#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Coin : public Module {
    public:
        Coin();

    private:

        // function definitions
        void get(Request &req, Response &res);
        void set(Request &req, Response &res);
        void insert(Request &req, Response &res);
        void blocker_get(Request &req, Response &res);
    };
}
