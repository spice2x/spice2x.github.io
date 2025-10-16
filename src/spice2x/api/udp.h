#pragma once
#include <atomic>
#include <external/kcp/ikcp.h>
#include <thread>

namespace api {
    constexpr uint32_t SPICEAPI_KCP_CONV_ID = 573;
    constexpr size_t SPICEAPI_UDP_BUFFER_SIZE = 64 * 1024;

    struct KcpClientState : ClientState {
        ikcpcb *kcp;
        std::string address_str;
        std::chrono::time_point<std::chrono::steady_clock> last_active;
        UdpController *controller;

        std::string recv_buf;
        std::vector<char> message_buffer;
    };

    class UdpController {
    public:
        UdpController(Controller *controller, uint16_t port);
        ~UdpController();
    private:
        void update_thread();
        void on_rawdata_recv(const sockaddr_in &sender, const char *data, size_t len);
        void update_states();
        void kcp_recv(KcpClientState *state);

        static int kcp_send_cb(const char *buf, int len, ikcpcb *kcp, void *user);
        static void kcp_log_cb(const char *log, ikcpcb *kcp, void *user);

        Controller *controller_;
        SOCKET socket_;
        uint16_t port_;

        std::thread update_thread_;
        std::atomic<bool> stop_signal_;

        std::vector<KcpClientState *> states_;
    };

} // namespace api
