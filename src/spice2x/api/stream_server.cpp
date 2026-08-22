#include <winsock2.h>
#include <ws2tcpip.h>

#include "stream_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "capture_pump.h"
#include "hooks/graphics/graphics.h"
#include "overlay/notifications.h"
#include "stream_format.h"
#include "util/logging.h"
#include "util/utils.h"

namespace api {

    namespace {

        struct HttpRequest {
            std::string method;
            std::string path;
            std::map<std::string, std::string> query;
        };

        bool send_all(SOCKET socket, const void *data, size_t size) {
            auto cursor = reinterpret_cast<const char *>(data);
            size_t remaining = size;

            while (remaining > 0) {
                const int sent = send(socket, cursor, static_cast<int>(remaining), 0);
                if (sent <= 0) {
                    return false;
                }
                cursor += sent;
                remaining -= static_cast<size_t>(sent);
            }

            return true;
        }

        bool send_all(SOCKET socket, const std::string &text) {
            return send_all(socket, text.data(), text.size());
        }

        // a viewer leaving is normally noticed by a failing send, so a stream with no frame
        // to push has to ask the socket instead
        bool client_gone(SOCKET socket) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(socket, &read_set);

            // the socket is blocking with a receive timeout, so poll before touching it
            timeval immediately {};
            const int ready = select(0, &read_set, nullptr, nullptr, &immediately);
            if (ready == 0) {
                return false;
            }
            if (ready < 0) {
                return true;
            }

            // consumed rather than peeked: a stray byte would otherwise sit in front of the
            // FIN and keep hiding it for as long as the stream runs
            char discard[256];
            return recv(socket, discard, sizeof(discard), 0) <= 0;
        }

        std::string url_decode(const std::string &input) {
            std::string out;
            out.reserve(input.size());

            for (size_t i = 0; i < input.size(); i++) {
                if (input[i] == '+') {
                    out.push_back(' ');
                } else if (input[i] == '%' && i + 2 < input.size()
                        && isxdigit(static_cast<unsigned char>(input[i + 1]))
                        && isxdigit(static_cast<unsigned char>(input[i + 2]))) {
                    out.push_back(static_cast<char>(
                            std::stoi(input.substr(i + 1, 2), nullptr, 16)));
                    i += 2;
                } else {
                    out.push_back(input[i]);
                }
            }

            return out;
        }

        void parse_query(const std::string &query, HttpRequest &request) {
            size_t pos = 0;

            while (pos < query.size()) {
                auto end = query.find('&', pos);
                if (end == std::string::npos) {
                    end = query.size();
                }

                const auto pair = query.substr(pos, end - pos);
                const auto split = pair.find('=');
                if (split != std::string::npos && split > 0) {
                    request.query[url_decode(pair.substr(0, split))] =
                            url_decode(pair.substr(split + 1));
                }

                pos = end + 1;
            }
        }

        // reads the request head only; anything oversized or malformed is refused
        bool read_request(SOCKET socket, size_t size_limit, HttpRequest &request) {
            std::string head;
            char buffer[1024];

            while (head.find("\r\n\r\n") == std::string::npos) {
                if (head.size() >= size_limit) {
                    return false;
                }

                // read no further than the limit, so the head cannot overshoot it
                const size_t budget = std::min(sizeof(buffer), size_limit - head.size());
                const int received = recv(socket, buffer, static_cast<int>(budget), 0);
                if (received <= 0) {
                    return false;
                }

                head.append(buffer, static_cast<size_t>(received));
            }

            const auto line_end = head.find("\r\n");
            const auto line = head.substr(0, line_end);

            const auto method_end = line.find(' ');
            if (method_end == std::string::npos) {
                return false;
            }

            const auto target_end = line.find(' ', method_end + 1);
            if (target_end == std::string::npos) {
                return false;
            }

            request.method = line.substr(0, method_end);
            auto target = line.substr(method_end + 1, target_end - method_end - 1);

            const auto query_start = target.find('?');
            if (query_start != std::string::npos) {
                parse_query(target.substr(query_start + 1), request);
                target = target.substr(0, query_start);
            }

            request.path = url_decode(target);
            return true;
        }

        int query_int(const HttpRequest &request, const std::string &name, int fallback,
                int min, int max) {

            const auto pos = request.query.find(name);
            if (pos == request.query.end()) {
                return fallback;
            }

            try {
                return std::clamp(std::stoi(pos->second), min, max);
            } catch (const std::exception &) {
                return fallback;
            }
        }

        // an <img> can show a cross-origin stream without this, but a browser client that
        // decodes the bytes itself has to fetch() them, and fetch is subject to CORS. errors
        // carry it too, or the client sees an opaque failure instead of the status.
        constexpr const char *cors_header = "Access-Control-Allow-Origin: *\r\n";

        // the port is unauthenticated, so a scanner hammering a busy/missing screen could
        // otherwise flood the overlay; throttle failure toasts per distinct cause. kept under
        // a second so it only swallows that, not a legitimate reconnect - substream itself
        // switches screens with a 300ms gap, and only backs off to a full second once a
        // retry has actually failed
        constexpr double notification_throttle_seconds = 0.5;

        void send_error(SOCKET socket, const char *status) {
            const std::string response =
                    std::string("HTTP/1.0 ") + status + "\r\n"
                    + cors_header +
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n";
            send_all(socket, response);
        }

        std::atomic<unsigned short> LISTENING_PORT { 0 };
    }

    unsigned short stream_server_port() {
        return LISTENING_PORT.load();
    }

    StreamServer::StreamServer(unsigned short port)
        : port(port)
    {
        // WinXP builds compile in neither encoder, so there would be nothing to serve and
        // every request would 404; taking the port instead only invites confused clients
        if (stream_formats().empty()) {
            log_warning("api::stream",
                    "this build has no video encoders, the video stream is unavailable");
            return;
        }

        if (!this->open_listener()) {
            // the stream was asked for explicitly, so say plainly that it is not there
            log_warning("api::stream",
                    "the video stream is not available on port {}", this->port);
            return;
        }

        this->running = true;
        this->acceptor = std::thread([this] {
            this->accept_worker();
        });

        LISTENING_PORT = this->port;

        // deliberately not logging a full URL; local IPs would leak into shared logs
        log_info("api::stream", "video stream is listening on port: {}", this->port);
        log_warning("api::stream",
                "the video stream is unauthenticated - anyone who can reach port {} can watch "
                "the game screen", this->port);
    }

    bool StreamServer::open_listener() {
        WSADATA wsa_data;
        const int error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (error != 0) {
            log_warning("api::stream", "WSAStartup() returned {}", error);
            return false;
        }
        this->wsa_started = true;

        this->listener = socket(AF_INET, SOCK_STREAM, 0);
        if (this->listener == INVALID_SOCKET) {
            log_warning("api::stream", "could not create listener socket: {}",
                    get_last_error_string());
            return false;
        }

        int opt_enable = 1;
        if (setsockopt(this->listener, SOL_SOCKET, SO_REUSEADDR,
                reinterpret_cast<const char *>(&opt_enable), sizeof(int)) == -1) {
            log_warning("api::stream", "could not set socket option SO_REUSEADDR: {}",
                    get_last_error_string());
        }

        sockaddr_in server_address {};
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(this->port);
        server_address.sin_addr.s_addr = INADDR_ANY;

        if (bind(this->listener, (sockaddr *) &server_address, sizeof(sockaddr)) == -1) {
            log_warning("api::stream", "could not bind socket on port {}: {}",
                    this->port, get_last_error_string());
            closesocket(this->listener);
            this->listener = INVALID_SOCKET;
            return false;
        }

        if (listen(this->listener, server_backlog) == -1) {
            log_warning("api::stream", "could not listen on port {}: {}",
                    this->port, get_last_error_string());
            closesocket(this->listener);
            this->listener = INVALID_SOCKET;
            return false;
        }

        return true;
    }

    StreamServer::~StreamServer() {

        this->running = false;
        LISTENING_PORT = 0;

        if (this->listener != INVALID_SOCKET) {
            closesocket(this->listener);
            this->listener = INVALID_SOCKET;
        }

        // drops the client threads out of their blocking send/recv
        {
            std::lock_guard<std::mutex> lock(this->clients_m);
            for (auto &client : this->clients) {
                if (client.socket != INVALID_SOCKET) {
                    ::shutdown(client.socket, SD_BOTH);
                }
            }
        }

        if (this->acceptor.joinable()) {
            this->acceptor.join();
        }

        // joining is what guarantees no client thread outlives this object
        for (auto &client : this->clients) {
            if (client.thread.joinable()) {
                client.thread.join();
            }
        }

        if (this->wsa_started) {
            WSACleanup();
        }
    }

    void StreamServer::accept_worker() {

        while (this->running) {
            sockaddr_in client_address {};
            int client_address_size = sizeof(sockaddr_in);

            const SOCKET client = accept(
                    this->listener, (sockaddr *) &client_address, &client_address_size);
            if (client == INVALID_SOCKET) {
                // on shutdown the listener is closed under us; otherwise do not spin
                if (this->running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                continue;
            }

            if (!this->running) {
                closesocket(client);
                break;
            }

            char address_data[INET_ADDRSTRLEN] {};
            inet_ntop(AF_INET, &client_address.sin_addr, address_data, INET_ADDRSTRLEN);
            std::string address(address_data);

            // every client costs an encode and real bandwidth, so the cap protects the game
            int slot = -1;
            {
                std::lock_guard<std::mutex> lock(this->clients_m);
                for (size_t i = 0; i < this->clients.size(); i++) {
                    if (!this->clients[i].active) {
                        this->clients[i].active = true;
                        this->clients[i].socket = client;
                        slot = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (slot < 0) {
                log_warning("api::stream", "client limit of {} hit", client_limit);
                overlay::notifications::add_throttled(
                        overlay::notifications::Severity::Warning,
                        "api::stream.client_limit",
                        notification_throttle_seconds,
                        fmt::format("Video stream refused: client limit reached ({})", address));
                send_error(client, "503 Service Unavailable");
                closesocket(client);
                continue;
            }

            // this thread is the only one that touches the thread objects, so the slot's
            // previous occupant gets reaped here rather than being detached
            if (this->clients[slot].thread.joinable()) {
                this->clients[slot].thread.join();
            }

            this->clients[slot].thread = std::thread([this, slot, client, address] {
                this->client_worker(slot, client, address);
            });
        }
    }

    void StreamServer::client_worker(int slot, SOCKET socket, std::string address) {

        DWORD timeout = request_timeout_ms;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char *>(&timeout), sizeof(timeout));

        timeout = send_timeout_ms;
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                reinterpret_cast<const char *>(&timeout), sizeof(timeout));

        int opt_enable = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                reinterpret_cast<const char *>(&opt_enable), sizeof(int));

        // whatever sits in the send buffer is already stale, and the default holds about a
        // third of a second of H.264 because the bitrate is so low. keeping it small makes a
        // slow reader block the sender, which then skips to the newest frame instead of
        // handing over a backlog
        int send_buffer = send_buffer_bytes;
        setsockopt(socket, SOL_SOCKET, SO_SNDBUF,
                reinterpret_cast<const char *>(&send_buffer), sizeof(send_buffer));

        HttpRequest request;
        if (read_request(socket, request_size_limit, request)) {
            if (request.method != "GET") {
                send_error(socket, "405 Method Not Allowed");
            } else {
                const int fps = query_int(request, "fps", 30, 1, fps_limit);
                const int quality = query_int(request, "q", 70, 1, 100);

                auto writer = make_stream_writer(request.path, quality, fps);
                if (!writer) {
                    send_error(socket, "404 Not Found");
                } else {
                    std::vector<int> screens;
                    graphics_screens_get(screens);

                    // registration takes a raw swapchain index and never bounds it, so the
                    // capture range has to be enforced here rather than assumed
                    const auto streamable = [&screens](int screen) {
                        return screen < static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO)
                                && std::find(screens.begin(), screens.end(), screen)
                                        != screens.end();
                    };

                    // screen 1 is the subscreen in every game that has one; single-screen games
                    // only ever register screen 0, so resolve the default against what exists.
                    // left unclamped so a nonsense screen is reported as what was asked for
                    int screen = query_int(request, "screen", -1, 0,
                            std::numeric_limits<int>::max());
                    if (screen < 0) {
                        screen = streamable(1) ? 1 : 0;
                    }

                    // the default always lands on a screen that exists, so this is only ever
                    // an explicit request for one that cannot be captured
                    if (!streamable(screen)) {
                        log_warning("api::stream",
                                "screen {} is not available, refusing {}", screen, address);
                        overlay::notifications::add_throttled(
                                overlay::notifications::Severity::Warning,
                                fmt::format("api::stream.screen_unavailable.{}", screen),
                                notification_throttle_seconds,
                                fmt::format("Video stream refused: screen {} not available ({})",
                                        screen, address));
                        send_error(socket, "404 Not Found");
                    } else if (!capture_pump::claim_screen(screen)) {
                        log_warning("api::stream",
                                "screen {} is already being streamed, refusing {}",
                                screen, address);
                        overlay::notifications::add_throttled(
                                overlay::notifications::Severity::Warning,
                                fmt::format("api::stream.screen_claimed.{}", screen),
                                notification_throttle_seconds,
                                fmt::format("Video stream refused: screen {} already streaming ({})",
                                        screen, address));
                        send_error(socket, "503 Service Unavailable");
                    } else {
                        log_info("api::stream",
                                "client connected: {} ({}, screen={}, fps={}, quality={})",
                                address, request.path, screen, fps, quality);
                        overlay::notifications::add(
                                overlay::notifications::Severity::Success,
                                fmt::format("Video stream client connected ({}, screen {})",
                                        address, screen));

                        const std::string header =
                                "HTTP/1.0 200 OK\r\n"
                                "Connection: close\r\n"
                                + std::string(cors_header) +
                                "Cache-Control: no-store, no-cache, must-revalidate\r\n"
                                "Pragma: no-cache\r\n"
                                "Content-Type: " + writer->content_type() + "\r\n"
                                "\r\n";

                        const StreamSend stream_send = [socket](const void *data, size_t size) {
                            return send_all(socket, data, size);
                        };

                        if (send_all(socket, header) && writer->begin(stream_send)) {
                            const auto interval = std::chrono::microseconds(1000000 / fps);

                            while (this->running) {
                                const auto started = std::chrono::steady_clock::now();

                                capture_pump::Frame frame;
                                const bool ok = capture_pump::capture_direct(
                                        screen, frame.pixels, 1,
                                        &frame.timestamp, &frame.width, &frame.height);

                                if (ok && frame.pixels) {
                                    if (!writer->write(stream_send, frame)) {
                                        break;
                                    }
                                } else if (client_gone(socket)) {
                                    break;
                                }

                                // a failed capture still paces, or a stalled game spins this
                                std::this_thread::sleep_until(started + interval);
                            }
                        }

                        capture_pump::release_screen(screen);
                        log_info("api::stream", "client disconnected: {}", address);
                        overlay::notifications::add(
                                overlay::notifications::Severity::Info,
                                fmt::format("Video stream client disconnected ({})", address));
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(this->clients_m);
            this->clients[slot].socket = INVALID_SOCKET;
            this->clients[slot].active = false;
        }

        closesocket(socket);
    }
}
