#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>

#include "util/rc4.h"

#include "module.h"
#include "websocket.h"
#include "serial.h"
#include "udp.h"

namespace api {

    struct ClientState {
        SOCKADDR_IN address;
        SOCKET socket;
        bool close = false;
        std::vector<Module*> modules;
        std::string password;
        bool password_change = false;
        util::RC4 *cipher = nullptr;
    };

    class Controller {
    private:

        // configuration
        const static int server_backlog = 16;
        const static int server_receive_buffer_size = 64 * 1024;
        const static int server_message_buffer_max_size = 64 * 1024;
        const static int server_worker_count = 2;
        const static int server_connection_limit = 4096;

        // settings
        unsigned short port;
        std::string password;
        bool pretty;

        // server
        WebSocketController *websocket;
        UdpController *udp;
        std::vector<SerialController *> serial;
        std::vector<std::thread> server_workers;
        std::vector<std::thread> server_handlers;
        std::mutex server_handlers_m;
        std::vector<api::ClientState *> client_states;
        std::mutex client_states_m;
        SOCKET server;
        void server_worker();
        void connection_handler(ClientState client_state);

    public:

        // state
        bool server_running;

        // constructor / destructor
        Controller(unsigned short port, std::string password, bool pretty);
        ~Controller();

        void listen_serial(std::string port, DWORD baud);

        bool process_request(ClientState *state, std::vector<char> *in, std::vector<char> *out);
        bool process_request(ClientState *state, const char *in, size_t in_size, std::vector<char> *out);
        static void process_password_change(ClientState *state);

        void init_state(ClientState *state);
        static void free_state(ClientState *state);

        void free_socket();
        void obtain_client_states(std::vector<ClientState> *output);

        std::string get_ip_address(sockaddr_in addr);

        inline const std::string &get_password() const {
            return this->password;
        }
    };
}
