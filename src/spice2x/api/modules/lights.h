#pragma once

#include <vector>
#include <external/robin_hood.h>

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
        robin_hood::unordered_map<std::string, std::reference_wrapper<Light>> lights_by_names;

        // function definitions
        void read(Request &req, Response &res);
        void write(Request &req, Response &res);
        void write_reset(Request &req, Response &res);

        // helper
        void get_light(Light &light, Response &res);
        bool write_light(std::string name, float state);
        bool write_light_reset(std::string name);
    };
}
