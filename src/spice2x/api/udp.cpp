#include "controller.h"
#include "util/logging.h"
#include "util/rc4.h"
#include "util/utils.h"

#include "ws2ipdef.h"

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

        sockaddr_in recv_addr;
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

        recv_thread_ = std::thread([this] {
            recv_thread();
        });

        log_info("api::udp", "UDP API server is listening on port: {}", this->port_);
    }

    UdpController::~UdpController() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }

        stop_signal_.store(true);
        recv_thread_.join();

        for (auto &state : states_) {
            ikcp_release(state.kcp);
        }

        states_.clear();
    }

    void UdpController::kcp_recv(std::vector<KcpClientState>::iterator iter) {
        auto &state = *iter;

        auto recv_len = ikcp_recv(state.kcp, state.recv_buf, SPICEAPI_UDP_RECV_BUFFER_SIZE);
        if (recv_len <= 0) {
            return;
        }

        if (state.cipher) {
            state.cipher->crypt(reinterpret_cast<uint8_t *>(state.recv_buf), recv_len);
        }

        if (LOGGING) {
            log_info("api::udp", "recv {}", bin2hex(state.recv_buf, recv_len));
        }

        // With KCP we can ensure that a packet is complete.
        if (state.recv_buf[recv_len - 1] != 0) {
            log_info("api::udp", "client {} sent an incomplete packet", state.address_str);
            return;
        }

        std::vector<char> send_buf;
        controller_->process_request(&state, std::span{state.recv_buf, state.recv_buf + recv_len}, &send_buf);

        if (state.cipher) {
            state.cipher->crypt(reinterpret_cast<uint8_t *>(send_buf.data()), send_buf.size());
        }

        if (send_buf.size()) {
            ikcp_send(state.kcp, send_buf.data(), send_buf.size());
        }

        controller_->process_password_change(&state);
    }

    void UdpController::recv_thread() {
        auto recv_buf = std::make_unique<char>(SPICEAPI_UDP_RECV_BUFFER_SIZE);

        while (!stop_signal_.load()) {
            auto clock = std::chrono::steady_clock::now();
            auto clock_ms = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(clock.time_since_epoch()).count() & 0xfffffffful);

            // update kcp stuffs
            for (auto iter = states_.begin(); iter < states_.end();) {
                auto &state = *iter;

                if (clock - state.last_active > 60s || ikcp_waitsnd(state.kcp) > 1000) {
                    log_info("api::udp", "client {} inactive, released", state.address_str);
                    ikcp_release(iter->kcp);
                    iter = states_.erase(iter);
                    continue;
                }

                ikcp_update(state.kcp, clock_ms);
                kcp_recv(iter);
                iter++;
            }

            fd_set readfds{
                .fd_count = 1,
                .fd_array = {socket_},
            };

            timeval timeout{
                .tv_sec = 0,
                .tv_usec = 10000,
            };

            auto select_ret = select(0, &readfds, nullptr, nullptr, &timeout);
            if (select_ret < 0) {
                log_warning("api::udp", "select error: {}", WSAGetLastError());
            }

            if (select_ret && FD_ISSET(socket_, &readfds)) {
                sockaddr_in sender_addr;
                int sender_size = sizeof(sender_addr);

                auto recv_len = recvfrom(
                    socket_,
                    recv_buf.get(),
                    SPICEAPI_UDP_RECV_BUFFER_SIZE,
                    0, reinterpret_cast<sockaddr *>(&sender_addr), &sender_size);

                if (recv_len < 0) {
                    log_warning("api::udp", "receive error: {}", WSAGetLastError());
                    continue;
                }

                if (LOGGING) {
                    log_info("api::udp", "recv raw buf {}", bin2hex(recv_buf.get(), recv_len));
                }

                auto iter = std::find_if(states_.begin(), states_.end(),
                                         [&sender_addr](const KcpClientState &state) {
                                             return state.address.sin_addr.s_addr == sender_addr.sin_addr.s_addr &&
                                                    state.address.sin_port == sender_addr.sin_port;
                                         });

                KcpClientState *state = nullptr;

                if (iter == states_.end()) {
                    char client_address_str_data[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sender_addr.sin_addr, client_address_str_data, INET_ADDRSTRLEN);

                    auto sender_addr_str = fmt::format("{}:{}", client_address_str_data, ntohs(sender_addr.sin_port));

                    auto &new_state = states_.emplace_back(KcpClientState{});
                    controller_->init_state(&new_state);

                    new_state.last_active = clock;
                    new_state.address_str = sender_addr_str;
                    new_state.address = sender_addr;
                    new_state.kcp = ikcp_create(SPICEAPI_KCP_CONV_ID, &new_state);
                    new_state.kcp->output = UdpController::kcp_send_cb;
                    new_state.controller = this;

                    if (LOGGING) {
                        new_state.kcp->writelog = UdpController::kcp_log_cb;
                        new_state.kcp->logmask = -1;
                    }

                    // fast mode
                    ikcp_nodelay(new_state.kcp, 1, 10, 2, 1);

                    log_info("api::udp", "new connection from {}", sender_addr_str);
                    state = &new_state;
                } else {
                    state = &*iter;
                    state->last_active = clock;
                }

                ikcp_input(state->kcp, recv_buf.get(), recv_len);
            }
        }
    }

    int UdpController::kcp_send_cb(const char *buf, int len, ikcpcb *kcp, void *user) {
        auto *state = reinterpret_cast<KcpClientState *>(user);
        return sendto(state->controller->socket_, buf, len, 0, reinterpret_cast<sockaddr *>(&state->address), sizeof(sockaddr_in));
    }

    void UdpController::kcp_log_cb(const char *log, ikcpcb *kcp, void *user) {
        auto *state = reinterpret_cast<KcpClientState *>(user);
        log_info("api::udp::kcp_log", "{}: {}", state->address_str, log);
    }
} // namespace api
