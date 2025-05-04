#include "iidx.h"
#include <functional>
#include <vector>
#include "games/iidx/iidx.h"
#include "external/rapidjson/document.h"

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

    void IIDX::tapeled_get(Request &req, Response &res) {
        static const std::string device_names[games::iidx::IIDX_TAPELED_TOTAL] = {
            "stage_left",
            "stage_right",
            "cabinet_left",
            "cabinet_right",
            "control_panel_under",
            "ceiling_left",
            "title_left",
            "title_right",
            "ceiling_right",
            "touch_panel_left",
            "touch_panel_right",
            "side_panel_left_inner",
            "side_panel_left_outer",
            "side_panel_left",
            "side_panel_right_outer",
            "side_panel_right_inner",
            "side_panel_right"
        };

        Value response_object(kObjectType);

        // Iterate through each device and dump its lights data into the response
        for (size_t device = 0; device < games::iidx::IIDX_TAPELED_TOTAL; device++) {
            const auto &data = games::iidx::TAPELED_MAPPING[device].data;

            Value light_state(kArrayType);
            for (const auto &led : data) {
                light_state.PushBack(led[0], res.doc()->GetAllocator());
                light_state.PushBack(led[1], res.doc()->GetAllocator());
                light_state.PushBack(led[2], res.doc()->GetAllocator());
            }

            response_object.AddMember(StringRef(device_names[device]), light_state, res.doc()->GetAllocator());
        }

        res.add_data(response_object);
    }
}
