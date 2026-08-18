#include <winsock2.h>
#include <ws2tcpip.h>

#include "stream_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <string>
#include <thread>

#include "capture_pump.h"
#include "hooks/graphics/graphics.h"
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
                if (head.size() > size_limit) {
                    return false;
                }

                const int received = recv(socket, buffer, sizeof(buffer), 0);
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

        void send_error(SOCKET socket, const char *status) {
            const std::string response =
                    std::string("HTTP/1.0 ") + status + "\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n";
            send_all(socket, response);
        }
    }

    StreamServer::StreamServer(unsigned short port)
        : port(port)
    {
        WSADATA wsa_data;
        const int error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (error != 0) {
            log_warning("api::stream", "WSAStartup() returned {}", error);
            return;
        }
        this->wsa_started = true;

        this->listener = socket(AF_INET, SOCK_STREAM, 0);
        if (this->listener == INVALID_SOCKET) {
            log_warning("api::stream", "could not create listener socket: {}",
                    get_last_error_string());
            return;
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
            return;
        }

        if (listen(this->listener, server_backlog) == -1) {
            log_warning("api::stream", "could not listen on port {}: {}",
                    this->port, get_last_error_string());
            closesocket(this->listener);
            this->listener = INVALID_SOCKET;
            return;
        }

        this->running = true;
        this->acceptor = std::thread([this] {
            this->accept_worker();
        });

        // deliberately not logging a full URL; local IPs would leak into shared logs
        log_info("api::stream", "MJPEG server is listening on port: {}", this->port);
        log_warning("api::stream",
                "the video stream is unauthenticated - anyone who can reach port {} can watch "
                "the game screen", this->port);
    }

    StreamServer::~StreamServer() {

        this->running = false;

        if (this->listener != INVALID_SOCKET) {
            closesocket(this->listener);
            this->listener = INVALID_SOCKET;
        }

        // drops the client threads out of their blocking send/recv
        {
            std::lock_guard<std::mutex> lock(this->clients_m);
            for (auto client : this->clients) {
                ::shutdown(client, SD_BOTH);
            }
        }

        if (this->acceptor.joinable()) {
            this->acceptor.join();
        }

        while (this->client_count.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // client threads own subscriptions, so the pumps can only stop once they are gone
        capture_pump::shutdown();

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

            // every client costs an encode and real bandwidth, so the cap protects the game
            if (this->client_count.load() >= client_limit) {
                log_warning("api::stream", "client limit of {} hit", client_limit);
                send_error(client, "503 Service Unavailable");
                closesocket(client);
                continue;
            }

            char address_data[INET_ADDRSTRLEN] {};
            inet_ntop(AF_INET, &client_address.sin_addr, address_data, INET_ADDRSTRLEN);
            std::string address(address_data);

            this->client_count++;
            {
                std::lock_guard<std::mutex> lock(this->clients_m);
                this->clients.push_back(client);
            }

            std::thread([this, client, address] {
                this->client_worker(client, address);
            }).detach();
        }
    }

    void StreamServer::client_worker(SOCKET socket, std::string address) {

        DWORD timeout = request_timeout_ms;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char *>(&timeout), sizeof(timeout));

        timeout = send_timeout_ms;
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                reinterpret_cast<const char *>(&timeout), sizeof(timeout));

        int opt_enable = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                reinterpret_cast<const char *>(&opt_enable), sizeof(int));

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
                    // screen 1 is the subscreen in every game that has one; single-screen games
                    // only ever register screen 0, so resolve the default against what exists
                    int screen = query_int(request, "screen", -1, 0,
                            static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO) - 1);
                    if (screen < 0) {
                        std::vector<int> screens;
                        graphics_screens_get(screens);
                        screen = std::find(screens.begin(), screens.end(), 1) != screens.end()
                                ? 1 : 0;
                    }

                    log_info("api::stream",
                            "client connected: {} ({}, screen={}, fps={}, quality={})",
                            address, request.path, screen, fps, quality);

                    const std::string header =
                            "HTTP/1.0 200 OK\r\n"
                            "Connection: close\r\n"
                            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
                            "Pragma: no-cache\r\n"
                            "Content-Type: " + writer->content_type() + "\r\n"
                            "\r\n";

                    const StreamSend stream_send = [socket](const void *data, size_t size) {
                        return send_all(socket, data, size);
                    };

                    if (send_all(socket, header) && writer->begin(stream_send)) {
                        capture_pump::Subscription subscription(screen, fps);

                        while (this->running) {
                            auto frame = subscription.next(1000);
                            if (!frame) {
                                continue;
                            }

                            if (!writer->write(stream_send, *frame)) {
                                break;
                            }
                        }
                    }

                    log_info("api::stream", "client disconnected: {}", address);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(this->clients_m);
            this->clients.erase(
                    std::remove(this->clients.begin(), this->clients.end(), socket),
                    this->clients.end());
        }

        closesocket(socket);
        this->client_count--;
    }
}
