#include "lights.h"
#include <functional>
#include <cfg/configurator.h>

#include "external/rapidjson/document.h"
#include "misc/eamuse.h"
#include "cfg/light.h"
#include "launcher/launcher.h"
#include "games/io.h"
#include "util/utils.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    Lights::Lights() : Module("lights") {
        functions["read"] = std::bind(&Lights::read, this, _1, _2);
        functions["write"] = std::bind(&Lights::write, this, _1, _2);
        functions["write_reset"] = std::bind(&Lights::write_reset, this, _1, _2);

        this->lights = games::get_lights(eamuse_get_game());
        for (auto &light : *this->lights) {
            this->lights_by_names.emplace(light.getName(), light);
        }
    }

    /**
     * read()
     * read([name: str], ...)
     */
    void Lights::read(api::Request &req, Response &res) {

        // check light cache
        if (!this->lights) {
            return;
        }

        // all lights for this game
        if (req.params.Size() == 0) {
            // add state for each light
            for (auto &light : *this->lights) {
                get_light(light, res);
            }

            return;
        }

        // specified light names
        for (Value &param : req.params.GetArray()) {
            // check params
            if (!param.IsArray()) {
                error(res, "parameters must be arrays");
                return;
            }
            if (param.Size() == 0) {
                error_params_insufficient(res);
                continue;
            }
            if (!param[0].IsString()) {
                error_type(res, "name", "string");
                continue;
            }
            const auto name = param[0].GetString();
            if (this->lights_by_names.contains(name)) {
                get_light(this->lights_by_names.at(name).get(), res);
            }
        }
    }

    void Lights::get_light(Light &light, Response &res) {
        Value state(kArrayType);
        Value light_name(light.getName().c_str(), res.doc()->GetAllocator());
        Value light_state(GameAPI::Lights::readLight(RI_MGR, light));
        Value light_enabled(light.override_enabled);
        state.PushBack(light_name, res.doc()->GetAllocator());
        state.PushBack(light_state, res.doc()->GetAllocator());
        state.PushBack(light_enabled, res.doc()->GetAllocator());
        res.add_data(state);
    }

    /**
     * write([name: str, state: float], ...)
     */
    void Lights::write(Request &req, Response &res) {

        // check light cache
        if (!this->lights) {
            return;
        }

        // loop parameters
        for (Value &param : req.params.GetArray()) {

            // check params
            if (!param.IsArray()) {
                error(res, "parameters must be arrays");
                return;
            }
            if (param.Size() < 2) {
                error_params_insufficient(res);
                continue;
            }
            if (!param[0].IsString()) {
                error_type(res, "name", "string");
                continue;
            }
            if (!param[1].IsFloat() && !param[1].IsInt()) {
                error_type(res, "state", "float");
                continue;
            }

            // get params
            auto light_name = param[0].GetString();
            auto light_state = param[1].GetFloat();

            // write light state
            if (!this->write_light(light_name, light_state)) {
                error_unknown(res, "light", light_name);
                continue;
            }
        }
    }

    /**
     * write_reset()
     * write_reset([name: str], ...)
     */
    void Lights::write_reset(Request &req, Response &res) {

        // check light cache
        if (!this->lights) {
            return;
        }

        // get params
        auto params = req.params.GetArray();

        // write_reset()
        if (params.Size() == 0) {
            if (lights != nullptr) {
                for (auto &light : *this->lights) {
                    if (light.override_enabled) {
                        if (cfg::CONFIGURATOR_STANDALONE) {
                            GameAPI::Lights::writeLight(RI_MGR, light, light.last_state);
                        }
                        light.override_enabled = false;
                    }
                }
            }
            return;
        }

        // loop parameters
        for (Value &param : req.params.GetArray()) {

            // check params
            if (!param.IsArray()) {
                error(res, "parameters must be arrays");
                return;
            }
            if (param.Size() < 1) {
                error_params_insufficient(res);
                continue;
            }
            if (!param[0].IsString()) {
                error_type(res, "name", "string");
                continue;
            }

            // get params
            auto light_name = param[0].GetString();

            // write analog state
            if (!this->write_light_reset(light_name)) {
                error_unknown(res, "analog", light_name);
                continue;
            }
        }
    }

    bool Lights::write_light(std::string name, float state) {

        // check light cache
        if (!this->lights) {
            return false;
        }

        // find light
        if (this->lights_by_names.contains(name)) {
            auto &light = this->lights_by_names.at(name).get();
            light.override_state = CLAMP(state, 0.f, 1.f);
            light.override_enabled = true;

            if (cfg::CONFIGURATOR_STANDALONE) {
                GameAPI::Lights::writeLight(RI_MGR, light, state);
            }

            return true;
        } else {
            // unknown light
            return false;
        }
    }

    bool Lights::write_light_reset(std::string name) {

        // check light cache
        if (!this->lights) {
            return false;
        }

        // find light
        if (this->lights_by_names.contains(name)) {
            auto &light = this->lights_by_names.at(name).get();
            light.override_enabled = false;
            return true;
        } else {
            // unknown light
            return false;
        }
    }
}
