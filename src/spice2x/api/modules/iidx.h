#pragma once

#include "api/module.h"
#include "api/request.h"
#include "games/iidx/iidx.h"

namespace api::modules {

    class IIDX : public Module {
    public:
        IIDX();

    private:
        // state
        robin_hood::unordered_map<std::string, std::reference_wrapper<tapeledutils::tape_led>> lights_by_names;

        // function definitions
        void ticker_get(Request &req, Response &res);
        void ticker_set(Request &req, Response &res);
        void ticker_reset(Request &req, Response &res);
        void tapeled_get(Request &req, Response &res);

        // helper
        void copy_tapeled_data(Response &res, rapidjson::Value &response_object, const tapeledutils::tape_led &mapping);
    };
}
