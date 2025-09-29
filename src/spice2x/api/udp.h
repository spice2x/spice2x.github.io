#pragma once
#include <atomic>
#include <external/kcp/ikcp.h>
#include <thread>

namespace api {
    constexpr uint32_t SPICEAPI_KCP_CONV_ID = 573;
    constexpr size_t SPICEAPI_UDP_RECV_BUFFER_SIZE = 64 * 1024;

    struct KcpClientState : ClientState {
        ikcpcb *kcp;
        std::string address_str;
        std::chrono::time_point<std::chrono::steady_clock> last_active;
        UdpController *controller;

        char recv_buf[SPICEAPI_UDP_RECV_BUFFER_SIZE];
    };

    class UdpController {
    public:
        UdpController(Controller *controller, uint16_t port);
        ~UdpController();

        void recv_thread();
        void kcp_recv(std::vector<KcpClientState>::iterator iter);

    private:
        static int kcp_send_cb(const char *buf, int len, ikcpcb *kcp, void *user);
        static void kcp_log_cb(const char *log, ikcpcb *kcp, void *user);

        Controller *controller_;
        SOCKET socket_;
        uint16_t port_;

        std::thread recv_thread_;
        std::atomic<bool> stop_signal_;

        std::vector<KcpClientState> states_;
    };

} // namespace api
