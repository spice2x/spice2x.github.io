#pragma once

#include <vector>
#include "api/module.h"
#include "api/request.h"
#include "cfg/api.h"

namespace api::modules {

    class Buttons : public Module {
    public:
        Buttons();

    private:

        // state
        std::vector<Button> *buttons;

        // function definitions
        void read(Request &req, Response &res);
        void write(Request &req, Response &res);
        void write_reset(Request &req, Response &res);

        // helper
        bool write_button(std::string name, float state);
        bool write_button_reset(std::string name);
    };
}
