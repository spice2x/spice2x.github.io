#include "controller.h"
#include "util/logging.h"
#include "util/rc4.h"
#include "util/utils.h"

// on msvc INET_ADDRSTRLEN was in ws2ipdef.h
#include <ws2ipdef.h>
// but on MINGW it was in ws2tcpip.h
#include <ws2tcpip.h>
// WTF!!!

#define KCP_LOGGING 0

using namespace std::chrono_literals;

namespace api {
    UdpController::UdpController(Controller *controller, uint16_t port) {
        controller_ = controller;
        port_ = port;

        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == INVALID_SOCKET) {
            log_warning("api::udp", "could not create listener socket: {}", get_last_error_string());
            return;
        }

        u_long nbio = 1;
        if (ioctlsocket(socket_, FIONBIO, &nbio) == SOCKET_ERROR) {
            log_warning("api::udp", "could not set socket to non-blocking mode: {}", get_last_error_string());

            closesocket(socket_);
            socket_ = INVALID_SOCKET;

            return;
        }

        sockaddr_in recv_addr{};
        recv_addr.sin_family = AF_INET;
        recv_addr.sin_port = htons(port_);
        recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        // bind socket to address
        if (bind(socket_, reinterpret_cast<sockaddr *>(&recv_addr), sizeof(sockaddr)) != 0) {
            log_warning("api::udp", "could not bind socket on port {}: {}", port, get_last_error_string());

            closesocket(socket_);
            socket_ = INVALID_SOCKET;

            return;
        }

        stop_signal_ = false;

        update_thread_ = std::thread([this] {
            update_thread();
        });

        log_info("api::udp", "UDP API server is listening on port: {}", this->port_);
    }

    UdpController::~UdpController() {
        stop_signal_.store(true);
        update_thread_.join();

        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }

        for (auto &state : states_) {
            ikcp_release(state->kcp);
            controller_->free_state(state);
            delete state;
        }

        states_.clear();
    }

    void UdpController::kcp_recv(KcpClientState *state) {
        // receive data
        auto recv_len = ikcp_recv(state->kcp, state->recv_buf.data(), state->recv_buf.size());
        if (recv_len <= 0) {
            return;
        }

        std::vector<char> message_buffer;

        // cipher
        if (state->cipher) {
            state->cipher->crypt(reinterpret_cast<uint8_t *>(state->recv_buf.data()), recv_len);
        }

        // put into buffer
        for (int i = 0; i < recv_len; i++) {

            // check for escape byte
            if (state->recv_buf[i] == 0) {

                // get response
                std::vector<char> send_buffer;
                controller_->process_request(state, &message_buffer, &send_buffer);

                // clear message buffer
                message_buffer.clear();

                // check send buffer for content
                if (!send_buffer.empty()) {

                    // cipher
                    if (state->cipher) {
                        state->cipher->crypt(reinterpret_cast<uint8_t *>(send_buffer.data()), send_buffer.size());
                    }

                    // send data
                    ikcp_send(state->kcp, send_buffer.data(), send_buffer.size());

                    // check for password change
                    controller_->process_password_change(state);
                }
            } else {

                // append to message
                message_buffer.push_back(state->recv_buf[i]);

                // check buffer size
                if (message_buffer.size() > SPICEAPI_UDP_BUFFER_SIZE) {
                    message_buffer.clear();
                    break;
                }
            }
        }
    }

    uint32_t get_clock_ms(std::chrono::steady_clock::time_point time) {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() & 0xfffffffful);
    }

    void UdpController::update_states() {
        const auto time = std::chrono::steady_clock::now();
        const auto time_ms = get_clock_ms(time);

        // update kcp stuffs
        for (auto iter = states_.begin(); iter != states_.end();) {
            auto *state = *iter;

            if (time - state->last_active > 2s || ikcp_waitsnd(state->kcp) > 100) {
                log_info("api::udp", "client {} inactive, released", state->address_str);
                ikcp_release(state->kcp);
                controller_->free_state(state);
                iter = states_.erase(iter);
                continue;
            }

            if (ikcp_check(state->kcp, time_ms) >= time_ms) {
                ikcp_update(state->kcp, time_ms);
                kcp_recv(state);
            }

            iter++;
        }
    }

    void UdpController::on_rawdata_recv(const sockaddr_in &sender, const char *data, size_t len) {
#if KCP_LOGGING
        if (LOGGING) {
            log_info("api::udp", "recv raw buf {}", bin2hex(data, len));
        }
#endif

        auto iter = std::find_if(states_.begin(), states_.end(),
                                 [&sender](const KcpClientState *state) {
                                     return state->address.sin_addr.s_addr == sender.sin_addr.s_addr &&
                                            state->address.sin_port == sender.sin_port;
                                 });

        KcpClientState *state = nullptr;

        if (iter == states_.end()) {
            char client_address_str_data[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender.sin_addr, client_address_str_data, INET_ADDRSTRLEN);

            state = new KcpClientState{};
            controller_->init_state(state);

            state->address = sender;
            state->address_str = fmt::format("{}:{}", client_address_str_data, ntohs(sender.sin_port));
            state->controller = this;
            state->recv_buf.resize(SPICEAPI_UDP_BUFFER_SIZE);

            state->kcp = ikcp_create(SPICEAPI_KCP_CONV_ID, state);
            state->kcp->output = UdpController::kcp_send_cb;

            // fast mode
            ikcp_nodelay(state->kcp, 1, 4, 2, 1);

#if KCP_LOGGING
            if (LOGGING) {
                state->kcp->writelog = UdpController::kcp_log_cb;
                state->kcp->logmask = -1;
            }
#endif

            log_info("api::udp", "new connection from {}", state->address_str);

            states_.push_back(state);
        } else {
            state = *iter;
        }

        state->last_active = std::chrono::steady_clock::now();
        ikcp_input(state->kcp, data, len);
    }

    void UdpController::update_thread() {
        std::string recv_buf{};
        recv_buf.resize(SPICEAPI_UDP_BUFFER_SIZE);

        while (!stop_signal_.load()) {
            sockaddr_in sender_addr;
            int sender_size = sizeof(sender_addr);

            auto recv_len = recvfrom(
                socket_,
                recv_buf.data(),
                recv_buf.size(),
                0, reinterpret_cast<sockaddr *>(&sender_addr), &sender_size);

            if (recv_len < 0) {
                int error = WSAGetLastError();

                if (error != WSAEWOULDBLOCK) {
                    log_warning("api::udp", "receive error: {}", WSAGetLastError());
                }
            } else {
                on_rawdata_recv(sender_addr, recv_buf.data(), recv_len);
            }

            update_states();
            Sleep(1);
        }
    }

    int UdpController::kcp_send_cb(const char *buf, int len, ikcpcb *kcp, void *user) {
        auto *state = reinterpret_cast<KcpClientState *>(user);
        return sendto(state->controller->socket_, buf, len, 0, reinterpret_cast<sockaddr *>(&state->address), sizeof(sockaddr_in));
    }

    void UdpController::kcp_log_cb(const char *log, ikcpcb *kcp, void *user) {
        auto *state = reinterpret_cast<KcpClientState *>(user);
        log_info("api::udp::kcp_log", "{}, {}", state->address_str, log);
    }
} // namespace api
