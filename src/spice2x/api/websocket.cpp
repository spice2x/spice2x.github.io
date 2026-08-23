#include <winsock2.h>
#include <ws2tcpip.h>

#include "websocket.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "external/wslay/msvc_compat.h"
#include <wslay/wslay.h>

#include "controller.h"
#include "external/hash-library/sha1.h"
#include "overlay/notifications.h"
#include "util/crypt.h"
#include "util/logging.h"

namespace api {

    namespace {

        constexpr int server_backlog = 4;
        constexpr size_t client_limit = 8;

        // a peer that connects and then says nothing must not hold a slot forever
        constexpr int handshake_timeout_ms = 5000;
        constexpr size_t request_size_limit = 8 * 1024;
        constexpr uint64_t message_size_limit = 64 * 1024;

        // how long a quiet connection waits before the loop rechecks whether we are stopping
        constexpr int idle_poll_ms = 500;

        // RFC 6455 appends this to the client key before hashing
        constexpr const char *websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        constexpr double notification_throttle_seconds = 0.5;

        std::string trim(const std::string &text) {
            const auto begin = text.find_first_not_of(" \t");
            if (begin == std::string::npos) {
                return "";
            }
            return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
        }

        std::string to_lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool contains_token(const std::string &value, const std::string &expected) {
            size_t pos = 0;
            while (pos < value.size()) {
                const size_t end = value.find(',', pos);
                if (to_lower(trim(value.substr(pos, end - pos))) == expected) {
                    return true;
                }
                if (end == std::string::npos) {
                    break;
                }
                pos = end + 1;
            }
            return false;
        }

        bool valid_websocket_key(const std::string &key) {
            if (key.size() != 24 || key[22] != '=' || key[23] != '=') {
                return false;
            }

            const std::string alphabet =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            for (size_t i = 0; i < 22; i++) {
                if (alphabet.find(key[i]) == std::string::npos) {
                    return false;
                }
            }
            return true;
        }

        bool send_all(SOCKET socket, const std::string &text) {
            size_t remaining = text.size();
            const char *cursor = text.data();

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

        void set_recv_timeout(SOCKET socket, int milliseconds) {
            const DWORD timeout = static_cast<DWORD>(milliseconds);
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char *>(&timeout), sizeof(timeout));
        }

        std::string accept_key(const std::string &client_key) {
            SHA1 sha1;
            sha1.add(client_key.data(), client_key.size());
            sha1.add(websocket_guid, strlen(websocket_guid));

            unsigned char digest[SHA1::HashBytes] {};
            sha1.getHash(digest);

            return crypt::base64_encode(digest, sizeof(digest));
        }

        bool handshake(SOCKET socket) {
            std::string request;
            char byte = 0;
            bool complete = false;

            while (request.size() < request_size_limit) {
                const int read = recv(socket, &byte, 1, 0);
                if (read <= 0) {
                    return false;
                }

                request.push_back(byte);
                if (request.size() >= 4
                        && request.compare(request.size() - 4, 4, "\r\n\r\n") == 0) {
                    complete = true;
                    break;
                }
            }

            std::string key;
            std::string version;
            bool connection_upgrade = false;
            bool upgrade = false;
            const size_t request_line_end = request.find("\r\n");
                const std::string request_line = request.substr(0, request_line_end);
                const size_t target_end = request_line.find(' ', 4);
                const bool valid_request_line = request_line_end != std::string::npos
                    && request_line.compare(0, 4, "GET ") == 0
                        && target_end != std::string::npos
                    && target_end > 4
                    && target_end == request_line.rfind(' ')
                    && request_line.substr(target_end + 1) == "HTTP/1.1";
            size_t pos = request_line_end;

            while (pos != std::string::npos) {
                const size_t end = request.find("\r\n", pos + 2);
                if (end == std::string::npos || end == pos + 2) {
                    break;
                }

                const std::string line = request.substr(pos + 2, end - pos - 2);
                const size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    const std::string name = to_lower(trim(line.substr(0, colon)));
                    const std::string value = trim(line.substr(colon + 1));

                    if (name == "sec-websocket-key") {
                        key = value;
                    } else if (name == "sec-websocket-version") {
                        version = value;
                    } else if (name == "connection") {
                        connection_upgrade = connection_upgrade
                                || contains_token(value, "upgrade");
                    } else if (name == "upgrade") {
                        upgrade = upgrade || contains_token(value, "websocket");
                    }
                }

                pos = end;
            }

                if (!complete || !valid_request_line || !connection_upgrade || !upgrade
                    || version != "13" || !valid_websocket_key(key)) {
                send_all(socket,
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n");
                return false;
            }

            // deliberately echoes back no extension or subprotocol: naming one the client did
            // not offer is a handshake failure, and none of them are wanted here
            return send_all(socket,
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: " + accept_key(key) + "\r\n"
                    "\r\n");
        }

        // everything one connection needs; wslay hands this back to the callbacks
        struct Session {
            SOCKET socket = INVALID_SOCKET;
            Controller *controller = nullptr;
            ClientState *state = nullptr;
            bool failed = false;
        };

        ssize_t recv_callback(wslay_event_context_ptr ctx, uint8_t *buffer, size_t length,
                int flags, void *user_data) {

            (void) flags;

            auto *session = static_cast<Session *>(user_data);
            const int read = recv(session->socket, reinterpret_cast<char *>(buffer),
                    static_cast<int>(length), 0);

            if (read > 0) {
                return read;
            }

            // the socket is non-blocking, so an empty one has to read as "nothing yet"
            // rather than as a dead peer
            if (read < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
                wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
                return -1;
            }

            wslay_event_set_error(ctx,
                    read == 0 ? WSLAY_ERR_NO_MORE_MSG : WSLAY_ERR_CALLBACK_FAILURE);
            return -1;
        }

        ssize_t send_callback(wslay_event_context_ptr ctx, const uint8_t *data, size_t length,
                int flags, void *user_data) {

            (void) flags;

            auto *session = static_cast<Session *>(user_data);
            const int sent = send(session->socket, reinterpret_cast<const char *>(data),
                    static_cast<int>(length), 0);

            if (sent > 0) {
                return sent;
            }

            if (sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
                wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
                return -1;
            }

            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return -1;
        }

        void on_msg_recv(wslay_event_context_ptr ctx,
                const struct wslay_event_on_msg_recv_arg *arg, void *user_data) {

            auto *session = static_cast<Session *>(user_data);

            // pings and closes are wslay's business, it answers them itself
            if (wslay_is_ctrl_frame(arg->opcode)) {
                return;
            }

            if (arg->opcode != WSLAY_BINARY_FRAME) {
                log_warning("api::websocket", "ignoring a non-binary message");
                return;
            }

            std::vector<char> in(arg->msg, arg->msg + arg->msg_length);
            std::vector<char> out;

            if (session->state->cipher) {
                session->state->cipher->crypt(
                        reinterpret_cast<uint8_t *>(in.data()), in.size());
            }

            session->controller->process_request(session->state, &in, &out);

            if (session->state->cipher) {
                session->state->cipher->crypt(
                        reinterpret_cast<uint8_t *>(out.data()), out.size());
            }

            wslay_event_msg reply {};
            reply.opcode = WSLAY_BINARY_FRAME;
            reply.msg = reinterpret_cast<const uint8_t *>(out.data());
            reply.msg_length = out.size();

            if (wslay_event_queue_msg(ctx, &reply) != 0) {
                session->failed = true;
                return;
            }

            Controller::process_password_change(session->state);
        }
    }

    struct WebSocketControllerState {
        struct Client {
            std::thread thread;
            SOCKET socket = INVALID_SOCKET;
            bool active = false;
        };

        Controller *controller = nullptr;
        unsigned short port = 0;
        SOCKET listener = INVALID_SOCKET;
        bool wsa_started = false;
        std::atomic_bool running { false };
        std::thread acceptor;
        std::mutex clients_m;
        std::array<Client, client_limit> clients;

        bool open_listener();
        void accept_worker();
        void client_worker(int slot, SOCKET socket, std::string address);
        void stop();
    };

    bool WebSocketControllerState::open_listener() {
        WSADATA wsa_data;
        const int error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (error != 0) {
            log_warning("api::websocket", "WSAStartup() returned {}", error);
            return false;
        }
        this->wsa_started = true;

        this->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (this->listener == INVALID_SOCKET) {
            log_warning("api::websocket", "socket() returned {}", WSAGetLastError());
            return false;
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(this->port);

        if (bind(this->listener, reinterpret_cast<sockaddr *>(&address), sizeof(address))
                == SOCKET_ERROR) {
            log_warning("api::websocket", "bind() returned {}", WSAGetLastError());
            return false;
        }

        if (listen(this->listener, server_backlog) == SOCKET_ERROR) {
            log_warning("api::websocket", "listen() returned {}", WSAGetLastError());
            return false;
        }

        return true;
    }

    void WebSocketControllerState::accept_worker() {

        while (this->running) {
            sockaddr_in client_address {};
            int client_address_size = sizeof(sockaddr_in);

            const SOCKET client = accept(
                    this->listener, reinterpret_cast<sockaddr *>(&client_address),
                    &client_address_size);

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

            // formatted by hand rather than with inet_ntop, which needs a newer Windows than
            // the XP toolchain targets, or inet_ntoa, which answers from a shared buffer
            const uint32_t raw = ntohl(client_address.sin_addr.s_addr);
            const std::string address = fmt::format("{}.{}.{}.{}",
                    (raw >> 24) & 0xff, (raw >> 16) & 0xff, (raw >> 8) & 0xff, raw & 0xff);

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
                log_warning("api::websocket", "client limit of {} hit", client_limit);
                overlay::notifications::add_throttled(
                        overlay::notifications::Severity::Warning,
                        "api::websocket.client_limit",
                        notification_throttle_seconds,
                        fmt::format("API websocket refused: client limit reached ({})", address));
                closesocket(client);
                continue;
            }

            // this thread is the only one that touches the thread objects, so the slot's
            // previous occupant gets reaped here rather than being detached
            if (this->clients[slot].thread.joinable()) {
                this->clients[slot].thread.join();
            }

            // the handshake runs on the client thread on purpose: doing it here would put
            // every later connection behind whatever this one is waiting for
            this->clients[slot].thread = std::thread([this, slot, client, address] {
                this->client_worker(slot, client, address);
            });
        }
    }

    void WebSocketControllerState::client_worker(int slot, SOCKET socket, std::string address) {

        // wslay writes a frame header and its payload as separate sends, so leaving Nagle on
        // holds the payload back until the peer acknowledges the header, costing a delayed
        // ack per message; requests here are small and latency sensitive
        int nodelay = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));

        set_recv_timeout(socket, handshake_timeout_ms);

        if (handshake(socket)) {
            // wslay reads until the socket would block, so leaving it blocking would make
            // every reply wait out a receive timeout before the send got a turn
            u_long non_blocking = 1;
            ioctlsocket(socket, FIONBIO, &non_blocking);

            Session session;
            session.socket = socket;
            session.controller = this->controller;
            session.state = new ClientState();
            this->controller->init_state(session.state);

            log_info("api::websocket", "client connected: {}", address);
            overlay::notifications::add(
                    overlay::notifications::Severity::Success,
                    fmt::format("API websocket client connected ({})", address));

            wslay_event_callbacks callbacks {};
            callbacks.recv_callback = recv_callback;
            callbacks.send_callback = send_callback;
            callbacks.on_msg_recv_callback = on_msg_recv;

            wslay_event_context_ptr ctx = nullptr;
            if (wslay_event_context_server_init(&ctx, &callbacks, &session) == 0) {
                wslay_event_config_set_max_recv_msg_length(ctx, message_size_limit);
                while (this->running && !session.failed
                        && (wslay_event_want_read(ctx) || wslay_event_want_write(ctx))) {

                    fd_set read_set;
                    fd_set write_set;
                    FD_ZERO(&read_set);
                    FD_ZERO(&write_set);

                    if (wslay_event_want_read(ctx)) {
                        FD_SET(socket, &read_set);
                    }
                    if (wslay_event_want_write(ctx)) {
                        FD_SET(socket, &write_set);
                    }

                    // bounded so a silent connection still notices us shutting down
                    timeval timeout {};
                    timeout.tv_usec = idle_poll_ms * 1000;

                    if (select(0, &read_set, &write_set, nullptr, &timeout) < 0) {
                        break;
                    }

                    if (FD_ISSET(socket, &read_set) && wslay_event_recv(ctx) != 0) {
                        break;
                    }

                    // unconditional, so a reply queued by the read above goes out now
                    if (wslay_event_send(ctx) != 0) {
                        break;
                    }
                }

                wslay_event_context_free(ctx);
            }

            Controller::free_state(session.state);
            delete session.state;

            log_info("api::websocket", "client disconnected: {}", address);
            overlay::notifications::add(
                    overlay::notifications::Severity::Info,
                    fmt::format("API websocket client disconnected ({})", address));
        }

        closesocket(socket);

        std::lock_guard<std::mutex> lock(this->clients_m);
        this->clients[slot].socket = INVALID_SOCKET;
        this->clients[slot].active = false;
    }

    void WebSocketControllerState::stop() {
        this->running = false;

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
            this->wsa_started = false;
        }
    }

    WebSocketController::WebSocketController(Controller *controller, uint16_t port) {
        this->controller = controller;

        this->state = new WebSocketControllerState();
        this->state->controller = controller;
        this->state->port = port;

        if (!this->state->open_listener()) {
            log_warning("api::websocket", "server failed to listen on port: {}", port);
            return;
        }

        this->state->running = true;
        this->state->acceptor = std::thread([this] {
            this->state->accept_worker();
        });

        log_info("api::websocket", "server listening on port: {}", port);
    }

    WebSocketController::~WebSocketController() {
        this->state->stop();
        delete this->state;
        this->state = nullptr;
    }

    void WebSocketController::free_socket() {
        this->state->stop();
    }
}
