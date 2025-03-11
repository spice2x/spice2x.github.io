#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class LCD : public Module {
    public:
        LCD();

    private:

        // function definitions
        void info(Request &req, Response &res);
    };
}
