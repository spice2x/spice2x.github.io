#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Resize : public Module {
    public:
        Resize();

    private:

        // function definitions
        void image_resize_enable(Request &req, Response &res);
        void image_resize_set_scene(Request &req, Response &res);
    };
}
