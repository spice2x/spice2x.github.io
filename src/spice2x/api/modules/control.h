#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Control : public Module {
    public:
        Control();

    private:

        // function definitions
        void raise(Request &req, Response &res);
        void exit(Request &req, Response &res);
        void restart(Request &req, Response &res);
        void session_refresh(Request &req, Response &res);
        void shutdown(Request &req, Response &res);
        void reboot(Request &req, Response &res);
    };
}
