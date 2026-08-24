#pragma once

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Card : public Module {
    public:
        Card();

    private:

        // function definitions
        void get_cards(Request &req, Response &res);
        void insert(Request &req, Response &res);
    };
}
