#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <winsock2.h>

namespace api {

    // 0 while no stream server is listening, so the API can tell clients not to look for one
    unsigned short stream_server_port();

    class StreamServer {
    public:

        explicit StreamServer(unsigned short port);
        ~StreamServer();

        StreamServer(const StreamServer &) = delete;
        StreamServer &operator=(const StreamServer &) = delete;

    private:

        // configuration
        static constexpr int server_backlog = 4;
        static constexpr int client_limit = 4;
        static constexpr int request_size_limit = 8 * 1024;
        static constexpr int request_timeout_ms = 5000;
        static constexpr int send_timeout_ms = 5000;
        // small enough that a low bitrate stream cannot hide a backlog of stale frames in it
        static constexpr int send_buffer_bytes = 16 * 1024;
        static constexpr int fps_limit = 60;

        struct Client {
            std::thread thread;
            SOCKET socket = INVALID_SOCKET;
            bool active = false;
        };

        void accept_worker();
        bool open_listener();
        void client_worker(int slot, SOCKET socket, std::string address);

        unsigned short port;
        SOCKET listener = INVALID_SOCKET;
        bool wsa_started = false;
        std::atomic_bool running { false };
        std::thread acceptor;
        std::mutex clients_m;
        // socket and active are guarded by clients_m; only the acceptor touches thread
        std::array<Client, client_limit> clients;
    };
}
