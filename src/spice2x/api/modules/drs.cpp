#include "drs.h"
#include <functional>
#include "external/rapidjson/document.h"
#include "games/drs/drs.h"

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    DRS::DRS() : Module("drs") {
        functions["tapeled_get"] = std::bind(&DRS::tapeled_get, this, _1, _2);
        functions["touch_set"] = std::bind(&DRS::touch_set, this, _1, _2);
    }

    /**
     * ticker_get()
     */
    void DRS::tapeled_get(Request &req, Response &res) {

        // copy data to array
        Value tapeled(kArrayType);
        const size_t tape_len = sizeof(games::drs::DRS_TAPELED);
        const uint8_t *tape_raw = (uint8_t*) games::drs::DRS_TAPELED;
        tapeled.Reserve(tape_len, res.doc()->GetAllocator());
        for (size_t i = 0; i < tape_len; i++) {
            tapeled.PushBack(tape_raw[i], res.doc()->GetAllocator());
        }

        // add to response
        res.add_data(tapeled);
    }

    void DRS::touch_set(Request &req, Response &res) {

        // get all touch points
        auto touches = std::make_unique<games::drs::drs_touch_t[]>(req.params.Size());
        size_t i = 0;
        for (Value &param : req.params.GetArray()) {

            // check params
            if (param.Size() < 6) {
                error_params_insufficient(res);
                continue;
            }
            if (!param[0].IsUint()) {
                error_type(res, "type", "uint");
                continue;
            }
            if (!param[1].IsUint()) {
                error_type(res, "id", "uint");
                continue;
            }
            if (!param[2].IsDouble()) {
                error_type(res, "x", "double");
                continue;
            }
            if (!param[3].IsDouble()) {
                error_type(res, "y", "double");
                continue;
            }
            if (!param[4].IsDouble()) {
                error_type(res, "width", "double");
                continue;
            }
            if (!param[5].IsDouble()) {
                error_type(res, "height", "double");
                continue;
            }

            // get params
            auto touch_type = param[0].GetUint();
            auto touch_id = param[1].GetUint();
            auto touch_x = param[2].GetDouble();
            auto touch_y = param[3].GetDouble();
            auto width = param[4].GetDouble();
            auto height = param[5].GetDouble();

            touches[i].type = touch_type;
            touches[i].id = touch_id;
            touches[i].x = touch_x;
            touches[i].y = touch_y;
            touches[i].width = width;
            touches[i].height = height;
            i++;
        }

        // apply touch points
        games::drs::fire_touches(touches.get(), i);
    }
}
