#pragma once

#include <vector>
#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class DRS : public Module {
    public:
        DRS();

    private:

        // function definitions
        void tapeled_get(Request &req, Response &res);
        void touch_set(Request &req, Response &res);
    };
}
