#include "controller.h"
#include "util/logging.h"
#include "util/rc4.h"
#include "util/utils.h"

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
    }

    void UdpController::recv_thread() {
        ClientState state{};
        controller_->init_state(&state);

        std::vector<char> message_buffer;
        std::vector<char> dummy_out;

        std::string receive_buffer;
        receive_buffer.resize(65536);

        while (!stop_signal_.load()) {
            struct sockaddr_in sender_addr;
            int sender_size = sizeof(sender_addr);

            auto received_length = recvfrom(socket_, receive_buffer.data(), receive_buffer.size(), 0, reinterpret_cast<sockaddr *>(&sender_addr), &sender_size);

            if (received_length < 0) {
                log_warning("api::udp", "receive error: {}", WSAGetLastError());
                continue;
            }

            // crypt in-data
            if (state.cipher) {
                state.cipher->crypt(reinterpret_cast<uint8_t *>(receive_buffer.data()), received_length);
            }

            // put into buffer
            for (int i = 0; i < received_length; i++) {

                // check for escape byte
                if (receive_buffer[i] == 0) {

                    // get response
                    std::vector<char> send_buffer;
                    controller_->process_request(&state, &message_buffer, &dummy_out);

                    // clear message buffer
                    message_buffer.clear();

                    // discard output as we don't know where's the client in udp
                    // ...for now?
                    dummy_out.clear();
                } else {

                    // append to message
                    message_buffer.push_back(receive_buffer[i]);

                    // check buffer size
                    if (message_buffer.size() > 65536) {
                        message_buffer.clear();

                        break;
                    }
                }
            }
        }

        controller_->free_state(&state);
    }
} // namespace api
