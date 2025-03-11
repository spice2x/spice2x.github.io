#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Memory : public Module {
    public:
        Memory();

    private:

        // function definitions
        void write(Request &req, Response &res);
        void read(Request &req, Response &res);
        void signature(Request &req, Response &res);
    };
}
