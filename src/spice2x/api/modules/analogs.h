#pragma once

#include <vector>
#include "api/module.h"
#include "api/request.h"
#include "cfg/api.h"

namespace api::modules {

    class Analogs : public Module {
    public:
        Analogs();

    private:

        // state
        std::vector<Analog> *analogs;

        // function definitions
        void read(Request &req, Response &res);
        void write(Request &req, Response &res);
        void write_reset(Request &req, Response &res);

        // helper
        bool write_analog(std::string name, float state);
        bool write_analog_reset(std::string name);
    };
}
