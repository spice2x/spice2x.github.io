#pragma once

#include <vector>
#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class DDR : public Module {
    public:
        DDR();

    private:
        // function definitions
        void tapeled_get(Request &req, Response &res);
    };
}
