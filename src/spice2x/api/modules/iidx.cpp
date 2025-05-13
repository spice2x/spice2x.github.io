#include "iidx.h"
#include <functional>

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    // settings
    static const size_t TICKER_SIZE = 9;

    IIDX::IIDX() : Module("iidx") {
        functions["ticker_get"] = std::bind(&IIDX::ticker_get, this, _1, _2);
        functions["ticker_set"] = std::bind(&IIDX::ticker_set, this, _1, _2);
        functions["ticker_reset"] = std::bind(&IIDX::ticker_reset, this, _1, _2);
        functions["tapeled_get"] = std::bind(&IIDX::tapeled_get, this, _1, _2);

        for (auto &light : games::iidx::TAPELED_MAPPING) {
            this->lights_by_names.emplace(light.lightName, light);
        }
    }

    /**
     * ticker_get()
     */
    void IIDX::ticker_get(api::Request &req, Response &res) {

        // get led ticker
        games::iidx::IIDX_LED_TICKER_LOCK.lock();
        Value led_ticker(StringRef(games::iidx::IIDXIO_LED_TICKER, TICKER_SIZE), res.doc()->GetAllocator());
        games::iidx::IIDX_LED_TICKER_LOCK.unlock();

        // add to response
        res.add_data(led_ticker);
    }

    /**
     * ticker_set(text: str)
     */
    void IIDX::ticker_set(api::Request &req, api::Response &res) {

        // check param
        if (req.params.Size() < 1)
            return error_params_insufficient(res);
        if (!req.params[0].IsString())
            return error_type(res, "text", "str");

        // get param
        auto text = req.params[0].GetString();
        auto text_len = req.params[0].GetStringLength();

        // lock
        std::lock_guard<std::mutex> ticker_lock(games::iidx::IIDX_LED_TICKER_LOCK);

        // set to read only
        games::iidx::IIDXIO_LED_TICKER_READONLY = true;

        // set led ticker
        memset(games::iidx::IIDXIO_LED_TICKER, ' ', TICKER_SIZE);
        for (size_t i = 0; i < TICKER_SIZE && i < text_len; i++) {
            games::iidx::IIDXIO_LED_TICKER[i] = text[i];
        }
    }

    void IIDX::ticker_reset(api::Request &req, api::Response &res) {

        // lock
        std::lock_guard<std::mutex> ticker_lock(games::iidx::IIDX_LED_TICKER_LOCK);

        // disable read only
        games::iidx::IIDXIO_LED_TICKER_READONLY = false;
    }

    /**
     * tapeled_get()
     * tapeled_get(name: str, ...)
     */
    void IIDX::tapeled_get(Request &req, Response &res) {
        Value response_object(kObjectType);

        // all tape leds
        if (req.params.Size() == 0) {
            // Iterate through each device and dump its lights data into the response
            for (const auto &mapping : games::iidx::TAPELED_MAPPING) {
                copy_tapeled_data(res, response_object, mapping);
            }
        } else {
            // specified light names
            for (Value &param : req.params.GetArray()) {
                // check params
                if (!param.IsString()) {
                    error_type(res, "name", "string");
                    return;
                }
                const auto name = param.GetString();
                if (const auto &it = lights_by_names.find(name); it != lights_by_names.end()) {
                    const auto mapping = it->second.get();
                    copy_tapeled_data(res, response_object, mapping);
                }
            }
        }

        res.add_data(response_object);
    }

    void IIDX::copy_tapeled_data(Response &res, Value &response_object, const tapeledutils::tape_led &mapping) {
        // Create an array for the light state
        Value light_state(kArrayType);
        light_state.Reserve(mapping.data.capacity() * 3, res.doc()->GetAllocator());
        for (const auto [r, g, b] : mapping.data) {
            light_state.PushBack(r, res.doc()->GetAllocator());
            light_state.PushBack(g, res.doc()->GetAllocator());
            light_state.PushBack(b, res.doc()->GetAllocator());
        }

        // Can't use StringRef here, turns some strings partially into null bytes for some reason
        Value light_name(mapping.lightName.c_str(), res.doc()->GetAllocator());
        response_object.AddMember(light_name, light_state, res.doc()->GetAllocator());
    }
}
