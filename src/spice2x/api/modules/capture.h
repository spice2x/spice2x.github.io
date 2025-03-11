#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Capture : public Module {
    public:
        Capture();

    private:

        // function definitions
        void get_screens(Request &req, Response &res);
        void get_jpg(Request &req, Response &res);
    };
}
