#pragma once

#include <functional>
#include <string>

#include "api/module.h"
#include "api/request.h"
#include "external/robin_hood.h"
#include "games/sdvx/sdvx.h"

namespace api::modules {

    class SDVX : public Module {
    public:
        SDVX();

    private:
        robin_hood::unordered_map<std::string, std::reference_wrapper<tapeledutils::tape_led>> lights_by_names;

        void tapeled_get(Request &req, Response &res);
        void copy_tapeled_data(Response &res, rapidjson::Value &response_object,
                const tapeledutils::tape_led &mapping);
    };
}
