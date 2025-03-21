#include <winsock2.h>
#include <ws2tcpip.h>

#include "controller.h"

#include <utility>

#include "cfg/configurator.h"
#include "external/rapidjson/document.h"
#include "util/crypt.h"
#include "util/logging.h"
#include "util/utils.h"

#include "module.h"
#include "modules/analogs.h"
#include "modules/buttons.h"
#include "modules/card.h"
#include "modules/capture.h"
#include "modules/coin.h"
#include "modules/control.h"
#include "modules/ddr.h"
#include "modules/drs.h"
#include "modules/iidx.h"
#include "modules/info.h"
#include "modules/keypads.h"
#include "modules/lcd.h"
#include "modules/lights.h"
#include "modules/memory.h"
#include "modules/touch.h"
#include "modules/resize.h"
#include "request.h"
#include "response.h"

using namespace rapidjson;
using namespace api;

Controller::Controller(unsigned short port, std::string password, bool pretty)
    : port(port), password(std::move(password)), pretty(pretty)
{
    if (!crypt::INITIALIZED && !this->password.empty()) {
        log_fatal("api", "API server with password cannot be used without crypt module");
    }

    // WSA startup
    WSADATA wsa_data;
    int error;
    if ((error = WSAStartup(MAKEWORD(2, 2), &wsa_data)) != 0) {
        log_warning("api", "WSAStartup() returned {}", error);
        this->server = INVALID_SOCKET;
        if (!cfg::CONFIGURATOR_STANDALONE) {
            log_fatal("api", "failed to start server");
        }
        return;
    }

    // create socket
    this->server = socket(AF_INET, SOCK_STREAM, 0);
    if (this->server == INVALID_SOCKET) {
        log_warning("api", "could not create listener socket: {}", get_last_error_string());
        if (!cfg::CONFIGURATOR_STANDALONE) {
            log_fatal("api", "failed to start server");
        }
        return;
    }

    // configure socket
    int opt_enable = 1;
    if (setsockopt(this->server, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char *>(&opt_enable), sizeof(int)) == -1)
    {
        log_warning("api", "could not set socket option SO_REUSEADDR: {}", get_last_error_string());
    }
    if (setsockopt(this->server, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char *>(&opt_enable), sizeof(int)) == -1)
    {
        log_warning("api", "could not set socket option TCP_NODELAY: {}", get_last_error_string());
    }

    // create address
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(this->port);
    server_address.sin_addr.s_addr = INADDR_ANY;
    memset(&server_address.sin_zero, 0, sizeof(server_address.sin_zero));

    // bind socket to address
    if (bind(this->server, (sockaddr *) &server_address, sizeof(sockaddr)) == -1) {
        log_warning("api", "could not bind socket on port {}: {}", port, get_last_error_string());
        this->server = INVALID_SOCKET;
        if (!cfg::CONFIGURATOR_STANDALONE) {
            log_fatal("api", "failed to start server");
        }
        return;
    }

    // set socket to listen
    if (listen(this->server, server_backlog) == -1) {
        log_warning("api", "could not listen to socket on port {}: {}", port, get_last_error_string());
        this->server = INVALID_SOCKET;
        if (!cfg::CONFIGURATOR_STANDALONE) {
            log_fatal("api", "failed to start server");
        }
        return;
    }

    // start workers
    this->server_running = true;
    for (int i = 0; i < server_worker_count; i++) {
        this->server_workers.emplace_back(std::thread([this] {
            this->server_worker();
        }));
    }

    // log success
    log_info("api", "API server is listening on port: {}", this->port);
    log_info("api", "Using password: {}", this->password.empty() ? "no" : "yes");

    // start websocket on next port
    this->websocket = new WebSocketController(this, port + 1);
}

Controller::~Controller() {

    // stop websocket
    delete this->websocket;

    // stop serial controllers
    for (auto &s : this->serial) {
        delete s;
    }

    // mark server stop
    this->server_running = false;

    // close socket
    if (this->server != INVALID_SOCKET) {
        closesocket(this->server);
    }

    // lock handlers
    std::lock_guard<std::mutex> handlers_guard(this->server_handlers_m);

    // join threads
    for (auto &worker : this->server_workers) {
        worker.join();
    }
    for (auto &handler : this->server_handlers) {
        handler.join();
    }

    // cleanup WSA
    WSACleanup();
}

void Controller::listen_serial(std::string port, DWORD baud) {
    this->serial.push_back(new SerialController(this, port, baud));
}

void Controller::server_worker() {

    // connection loop
    while (this->server_running) {

        // create client state
        ClientState client_state {};

        // accept connection
        int socket_in_size = sizeof(sockaddr_in);
        client_state.socket = accept(this->server, (sockaddr *) &client_state.address, &socket_in_size);
        if (client_state.socket == INVALID_SOCKET) {
            continue;
        }

        // lock handlers
        std::lock_guard<std::mutex> handlers_guard(this->server_handlers_m);

        // check connection limit
        if (this->server_handlers.size() >= server_connection_limit) {
            log_warning("api", "connection limit hit");
            closesocket(client_state.socket);
            continue;
        }

        // handle connection
        this->server_handlers.emplace_back(std::thread([this, client_state] {
            this->connection_handler(client_state);
        }));
    }
}

void Controller::connection_handler(api::ClientState client_state) {

    // get address string
    char client_address_str_data[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_state.address.sin_addr, client_address_str_data, INET_ADDRSTRLEN);
    std::string client_address_str(client_address_str_data);

    // log connection
    log_info("api", "client connected: {}", client_address_str);
    client_states_m.lock();
    client_states.emplace_back(&client_state);
    client_states_m.unlock();

    // init state
    init_state(&client_state);

    // listen loop
    std::vector<char> message_buffer;
    char receive_buffer[server_receive_buffer_size];
    while (this->server_running && !client_state.close) {

        // receive data
        int received_length = recv(client_state.socket, receive_buffer, server_receive_buffer_size, 0);
        if (received_length < 0) {

            // if the received length is < 0, we've got an error
            log_warning("api", "receive error: {}", WSAGetLastError());
            break;
        } else if (received_length == 0) {

            // if the received length is 0, the connection is closed
            break;
        }

        // cipher
        if (client_state.cipher != nullptr) {
            client_state.cipher->crypt(
                    (uint8_t *) receive_buffer,
                    (size_t) received_length
            );
        }

        // put into buffer
        for (int i = 0; i < received_length; i++) {

            // check for escape byte
            if (receive_buffer[i] == 0) {

                // get response
                std::vector<char> send_buffer;
                this->process_request(&client_state, &message_buffer, &send_buffer);

                // clear message buffer
                message_buffer.clear();

                // check send buffer for content
                if (!send_buffer.empty()) {

                    // cipher
                    if (client_state.cipher != nullptr) {
                        client_state.cipher->crypt(
                                (uint8_t *) send_buffer.data(),
                                (size_t) send_buffer.size()
                        );
                    }

                    // send data
                    send(client_state.socket, send_buffer.data(), (int) send_buffer.size(), 0);

                    // check for password change
                    process_password_change(&client_state);
                }
            } else {

                // append to message
                message_buffer.push_back(receive_buffer[i]);

                // check buffer size
                if (message_buffer.size() > server_message_buffer_max_size) {
                    message_buffer.clear();
                    client_state.close = true;
                    break;
                }
            }
        }
    }

    // log disconnect
    log_info("api", "client disconnected: {}", client_address_str);
    client_states_m.lock();
    client_states.erase(std::remove(client_states.begin(), client_states.end(), &client_state));
    client_states_m.unlock();

    // close connection
    closesocket(client_state.socket);

    // free state
    free_state(&client_state);
}

bool Controller::process_request(ClientState *state, std::vector<char> *in, std::vector<char> *out) {
    return this->process_request(state, &(*in)[0], in->size(), out);
}

bool Controller::process_request(ClientState *state, const char *in, size_t in_size, std::vector<char> *out) {

    // parse document
    Document document;
    document.Parse(in, in_size);

    // check for parse error
    if (document.HasParseError()) {

        // return empty response and close connection
        out->push_back(0);
        state->close = true;
        return false;
    }

    // build request and response
    Request request(document);
    Response response(request.id);
    bool success = true;

    // check if request has parse error
    if (request.parse_error) {
        Value module_error("Request parse error (invalid message format?).");
        response.add_error(module_error);
        success = false;
    } else {

        // find module
        bool module_found = false;
        for (auto module : state->modules) {
            if (module->name == request.module) {
                module_found = true;

                // check password force
                if (module->password_force && this->password.empty() && request.function != "session_refresh") {
                    Value err("Module requires the password to be set.");
                    response.add_error(err);
                    break;
                }

                // handle request
                module->handle(request, response);
                break;
            }
        }

        // check if module wasn't found
        if (!module_found) {
            Value module_error("Unknown module.");
            response.add_error(module_error);
        }

        // check for password change
        if (response.password_changed) {
            state->password = response.password;
            state->password_change = true;
        }
    }

    // write response
    auto response_out = response.get_string(this->pretty);
    out->insert(out->end(), response_out.begin(), response_out.end());
    out->push_back(0);
    return success;
}

void Controller::process_password_change(api::ClientState *state) {

    // check for password change
    if (state->password_change) {
        state->password_change = false;
        delete state->cipher;
        if (state->password.empty()) {
            state->cipher = nullptr;
        } else {
            state->cipher = new util::RC4(
                    (uint8_t *) state->password.c_str(),
                    state->password.size());
        }
    }
}

void Controller::init_state(api::ClientState *state) {

    // check if already initialized
    if (!state->modules.empty()) {
        log_fatal("api", "client state double initialization");
    }

    // cipher
    state->cipher = nullptr;
    state->password = this->password;
    if (!this->password.empty()) {
        state->cipher = new util::RC4((uint8_t *) this->password.c_str(), this->password.size());
    }

    // create module instances
    state->modules.push_back(new modules::Analogs());
    state->modules.push_back(new modules::Buttons());
    state->modules.push_back(new modules::Card());
    state->modules.push_back(new modules::Capture());
    state->modules.push_back(new modules::Coin());
    state->modules.push_back(new modules::Control());
    state->modules.push_back(new modules::DDR());
    state->modules.push_back(new modules::DRS());
    state->modules.push_back(new modules::IIDX());
    state->modules.push_back(new modules::Info());
    state->modules.push_back(new modules::Keypads());
    state->modules.push_back(new modules::LCD());
    state->modules.push_back(new modules::Lights());
    state->modules.push_back(new modules::Memory());
    state->modules.push_back(new modules::Touch());
    state->modules.push_back(new modules::Resize());
}

void Controller::free_state(api::ClientState *state) {

    // free modules
    for (auto module : state->modules) {
        delete module;
    }

    // free cipher
    delete state->cipher;
}

void Controller::free_socket() {
    if (this->server != INVALID_SOCKET) {
        closesocket(this->server);
        this->server = INVALID_SOCKET;
    }

    this->websocket->free_socket();

    for (auto &s : this->serial) {
        s->free_port();
    }
}

void Controller::obtain_client_states(std::vector<ClientState> *vec) {
    std::lock_guard<std::mutex> lock(this->client_states_m);
    for (auto &state : this->client_states) {
        vec->push_back(*state);
    }
}

std::string Controller::get_ip_address(sockaddr_in addr) {
    switch (addr.sin_family) {
        default:
        case AF_INET: {
            char buf[INET_ADDRSTRLEN];
            auto ret = inet_ntop(AF_INET, &(addr.sin_addr), buf, sizeof(buf));
            if (ret != nullptr) {
                return std::string(ret);
            } else {
                return "unknown (" + to_string(WSAGetLastError()) + ")";
            }
        }
        case AF_INET6: {
            char buf[INET6_ADDRSTRLEN];
            auto ret = inet_ntop(AF_INET6, &(addr.sin_addr), buf, sizeof(buf));
            if (ret != nullptr) {
                return std::string(ret);
            } else {
                return "unknown (" + to_string(WSAGetLastError()) + ")";
            }
        }
    }
}
