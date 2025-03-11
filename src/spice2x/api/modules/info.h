#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Info : public Module {
    public:
        Info();

    private:

        // function definitions
        void avs(Request &req, Response &res);
        void launcher(Request &req, Response &res);
        void memory(Request &req, Response &res);
    };
}
