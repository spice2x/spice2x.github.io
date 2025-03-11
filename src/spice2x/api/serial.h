#pragma once

#include <string>
#include <thread>
#include <windows.h>

namespace api {

    class Controller;
    struct ClientState;

    class SerialController {
    public:

        SerialController(Controller *controller, std::string port, DWORD baud);
        ~SerialController();

        void open_port();
        void free_port();

    private:
        Controller *controller = nullptr;
        std::string port = "";
        DWORD baud = 0;
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::thread *thread = nullptr;
        ClientState *state = nullptr;
        bool running = true;
    };
}
