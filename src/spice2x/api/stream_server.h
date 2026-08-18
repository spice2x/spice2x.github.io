#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>

namespace api {

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

        void accept_worker();
        void client_worker(SOCKET socket, std::string address);

        unsigned short port;
        SOCKET listener = INVALID_SOCKET;
        bool wsa_started = false;
        std::atomic_bool running { false };
        std::atomic_int client_count { 0 };
        std::thread acceptor;
        std::mutex clients_m;
        std::vector<SOCKET> clients;
    };
}
