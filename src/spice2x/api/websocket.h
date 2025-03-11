#pragma once

#include <cstdint>
#include <thread>

namespace api {

    struct WebSocketControllerState;
    class Controller;

    class WebSocketController {
    public:

        WebSocketController(Controller *controller, uint16_t port);
        ~WebSocketController();

        void free_socket();
        Controller *controller = nullptr;
        WebSocketControllerState *state = nullptr;
    };
}
