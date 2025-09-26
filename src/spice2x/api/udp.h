#pragma once
#include <atomic>
#include <thread>

namespace api {
    class Controller;

    class UdpController {
    public:
        UdpController(Controller *controller, uint16_t port);
        ~UdpController();

        void recv_thread();

    private:
        Controller *controller_;
        SOCKET socket_;
        uint16_t port_;

        std::thread recv_thread_;
        std::atomic<bool> stop_signal_;
    };

} // namespace api
