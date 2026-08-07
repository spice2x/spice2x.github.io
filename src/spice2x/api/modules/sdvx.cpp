#include "sdvx.h"

#include <functional>

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    SDVX::SDVX() : Module("sdvx") {
        functions["tapeled_get"] = std::bind(&SDVX::tapeled_get, this, _1, _2);

        for (auto &light : games::sdvx::TAPELED_MAPPING) {
            lights_by_names.emplace(light.lightName, light);
        }
    }

    /**
     * tapeled_get()
     * tapeled_get(name: str, ...)
     */
    void SDVX::tapeled_get(Request &req, Response &res) {
        Value response_object(kObjectType);

        if (req.params.Size() == 0) {
            for (const auto &mapping : games::sdvx::TAPELED_MAPPING) {
                copy_tapeled_data(res, response_object, mapping);
            }
        } else {
            for (Value &param : req.params.GetArray()) {
                if (!param.IsString()) {
                    error_type(res, "name", "string");
                    return;
                }

                const auto name = param.GetString();
                if (const auto &it = lights_by_names.find(name); it != lights_by_names.end()) {
                    copy_tapeled_data(res, response_object, it->second.get());
                }
            }
        }

        res.add_data(response_object);
    }

    void SDVX::copy_tapeled_data(Response &res, Value &response_object,
            const tapeledutils::tape_led &mapping)
    {
        Value light_state(kArrayType);
        light_state.Reserve(mapping.data.size() * 3, res.doc()->GetAllocator());
        for (const auto [r, g, b] : mapping.data) {
            light_state.PushBack(r, res.doc()->GetAllocator());
            light_state.PushBack(g, res.doc()->GetAllocator());
            light_state.PushBack(b, res.doc()->GetAllocator());
        }

        Value light_name(mapping.lightName.c_str(), res.doc()->GetAllocator());
        response_object.AddMember(light_name, light_state, res.doc()->GetAllocator());
    }
}