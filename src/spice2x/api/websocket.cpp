#define HEADSOCKET_IMPLEMENTATION
#include "external/headsocket.h"

#include "websocket.h"
#include "util/utils.h"
#include "util/rc4.h"
#include "util/logging.h"
#include "controller.h"

using namespace headsocket;

namespace api {

    /*
     * Client class declaration
     */
    class WebSocketClient : public web_socket_client {

        // required class header
        HEADSOCKET_CLIENT(WebSocketClient, web_socket_client);

    private:
        ClientState *state = nullptr;

    protected:
        bool async_received_data(const data_block &db, uint8_t *ptr, size_t length) override;

        void on_accept() override;
        void on_disconnect() override;
    };

    /*
     * Server class declaration
     */
    class WebSocketServer : public web_socket_server<WebSocketClient> {
        HEADSOCKET_SERVER(WebSocketServer, web_socket_server);
    public:
        WebSocketController *websocket;
    };

    void api::WebSocketServer::init() {}

    /*
     * Controller state so we don't have to import headsocket stuff in our header
     */
    struct WebSocketControllerState {
        std::shared_ptr<WebSocketServer> server;
    };

    WebSocketController::WebSocketController(Controller *controller, uint16_t port) {
        this->controller = controller;

        // create state
        this->state = new WebSocketControllerState();

        // start server
        this->state->server = WebSocketServer::create(port);
        this->state->server->websocket = this;
        if (this->state->server->is_running()) {
            log_info("api::websocket", "server listening on port: {}", port);
        } else {
            log_warning("api::websocket", "server failed to listen on port: {}", port);
        }
    }

    WebSocketController::~WebSocketController() {

        // stop server
        this->state->server->stop();

        // delete state
        delete this->state;
    }

    void WebSocketController::free_socket() {
        this->state->server->stop();
    }

    void WebSocketClient::on_accept() {
        web_socket_client::on_accept();

        // get pointer to server
        auto srv = reinterpret_cast<WebSocketServer *>(server().get());
        if (!srv || !srv->websocket) {
            log_fatal("api::websocket", "on_accept has no server");
        }

        // check for init
        state = new ClientState();
        srv->websocket->controller->init_state(state);

        // log connection
        log_info("api::websocket", "client connected");
    }

    void WebSocketClient::on_disconnect() {

        // log disconnection
        log_info("api::websocket", "client disconnected");

        // get pointer to server
        auto srv = reinterpret_cast<WebSocketServer *>(server().get());
        if (!srv || !srv->websocket) {
            log_fatal("api::websocket", "on_disconnect has no server");
        }

        // clean up state
        srv->websocket->controller->free_state(state);
        delete state;
        state = nullptr;

        // call super
        web_socket_client::on_disconnect();
    }

    /*
     * This is where business actually happens, gets called on every datablock receive
     */
    bool WebSocketClient::async_received_data(const data_block &db, uint8_t *ptr, size_t length) {

        // get pointer to server
        auto srv = reinterpret_cast<WebSocketServer *>(server().get());
        if (!srv || !srv->websocket) {
            log_fatal("api::websocket", "received datablock without server");
        }

        // check state
        if (!state) {
            log_fatal("api::websocket", "client with no state received datablock");
        }

        // check datablock type
        switch (db.op) {
            case opcode::binary: {

                // allocate buffers
                std::vector<char> in(ptr, ptr + length);
                std::vector<char> out;

                // crypt in-data
                if (state->cipher) {
                    state->cipher->crypt(reinterpret_cast<uint8_t *>(in.data()), in.size());
                }

                // process request
                srv->websocket->controller->process_request(state, &in, &out);

                // crypt out-data
                if (state->cipher) {
                    state->cipher->crypt(reinterpret_cast<uint8_t *>(out.data()), out.size());
                }

                // send answer
                push(out.data(), out.size());

                // check for password change
                srv->websocket->controller->process_password_change(state);

                break;
            }
            default:
                log_warning("api::websocket", "datablock received with non-binary type");
                break;
        }

        // always consume the datablock, nomnom
        return true;
    }
}
