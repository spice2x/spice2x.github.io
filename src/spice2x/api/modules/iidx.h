#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class IIDX : public Module {
    public:
        IIDX();

    private:

        // function definitions
        void ticker_get(Request &req, Response &res);
        void ticker_set(Request &req, Response &res);
        void ticker_reset(Request &req, Response &res);
        void tapeled_get(Request &req, Response &res);
    };
}
