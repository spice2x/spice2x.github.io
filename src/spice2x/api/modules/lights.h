#pragma once

#include <vector>
#include "api/module.h"
#include "api/request.h"
#include "cfg/api.h"

namespace api::modules {

    class Lights : public Module {
    public:
        Lights();

    private:

        // state
        std::vector<Light> *lights;

        // function definitions
        void read(Request &req, Response &res);
        void write(Request &req, Response &res);
        void write_reset(Request &req, Response &res);

        // helper
        bool write_light(std::string name, float state);
        bool write_light_reset(std::string name);
    };
}
