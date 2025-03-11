#include "analogs.h"
#include <functional>
#include "external/rapidjson/document.h"
#include "misc/eamuse.h"
#include "cfg/analog.h"
#include "launcher/launcher.h"
#include "games/io.h"
#include "util/utils.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    Analogs::Analogs() : Module("analogs") {
        functions["read"] = std::bind(&Analogs::read, this, _1, _2);
        functions["write"] = std::bind(&Analogs::write, this, _1, _2);
        functions["write_reset"] = std::bind(&Analogs::write_reset, this, _1, _2);
        analogs = games::get_analogs(eamuse_get_game());
    }

    /**
     * read()
     */
    void Analogs::read(api::Request &req, Response &res) {

        // check analog cache
        if (!analogs) {
            return;
        }

        // add state for each analog
        for (auto &analog : *this->analogs) {
            Value state(kArrayType);
            Value analog_name(analog.getName().c_str(), res.doc()->GetAllocator());
            Value analog_state(GameAPI::Analogs::getState(RI_MGR, analog));
            Value analog_enabled(analog.override_enabled);
            state.PushBack(analog_name, res.doc()->GetAllocator());
            state.PushBack(analog_state, res.doc()->GetAllocator());
            state.PushBack(analog_enabled, res.doc()->GetAllocator());
            res.add_data(state);
        }
    }

    /**
     * write([name: str, state: float], ...)
     */
    void Analogs::write(Request &req, Response &res) {

        // check analog cache
        if (!analogs)
            return;

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
            auto analog_name = param[0].GetString();
            auto analog_state = param[1].GetFloat();

            // write analog state
            if (!this->write_analog(analog_name, analog_state)) {
                error_unknown(res, "analog", analog_name);
                continue;
            }
        }
    }

    /**
     * write_reset()
     * write_reset([name: str], ...)
     */
    void Analogs::write_reset(Request &req, Response &res) {

        // check analog cache
        if (!analogs)
            return;

        // get params
        auto params = req.params.GetArray();

        // write_reset()
        if (params.Size() == 0) {
            if (analogs != nullptr) {
                for (auto &analog : *this->analogs) {
                    analog.override_enabled = false;
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
            auto analog_name = param[0].GetString();

            // write analog state
            if (!this->write_analog_reset(analog_name)) {
                error_unknown(res, "analog", analog_name);
                continue;
            }
        }
    }

    bool Analogs::write_analog(std::string name, float state) {

        // check analog cache
        if (!this->analogs) {
            return false;
        }

        // find analog
        for (auto &analog : *this->analogs) {
            if (analog.getName() == name) {
                analog.override_state = CLAMP(state, 0.f, 1.f);
                analog.override_enabled = true;
                return true;
            }
        }

        // unknown analog
        return false;
    }

    bool Analogs::write_analog_reset(std::string name) {

        // check analog cache
        if (!analogs) {
            return false;
        }

        // find analog
        for (auto &analog : *this->analogs) {
            if (analog.getName() == name) {
                analog.override_enabled = false;
                return true;
            }
        }

        // unknown analog
        return false;
    }
}
