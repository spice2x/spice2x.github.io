#include "touch.h"

#include <cstdint>
#include <functional>

#include "external/rapidjson/document.h"
#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "misc/eamuse.h"
#include "launcher/launcher.h"
#include "touch/touch.h"
#include "touch/native/inject.h"
#include "touch/native/nativetouchhook.h"
#include "util/utils.h"
#include "games/iidx/iidx.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    static void inject_native_touch(
            const std::vector<TouchPoint> &touch_points, int canvas_w, int canvas_h) {
        if (touch_points.empty()) {
            // no active touches remain; release the synthetic contact
            nativetouch::inject::inject_synthetic_touch({ 0, 0 }, false);
            return;
        }

        // only a single synthetic contact is supported, so just use the first touch point
        const auto &touch = touch_points.front();
        POINT position { touch.x, touch.y };
        if (canvas_w > 0 && canvas_h > 0) {
            nativetouch::inject::inject_synthetic_touch_from_canvas(
                position,
                { canvas_w, canvas_h },
                true);
        } else {
            nativetouch::inject::inject_synthetic_touch(position, true);
        }
    }

    Touch::Touch() : Module("touch") {
        is_sdvx = avs::game::is_model("KFC");

        is_tdj_fhd = (avs::game::is_model("LDJ") && games::iidx::is_tdj_fhd());
        // special case: when windowed subscreen is in use, use the original coords
        if (GRAPHICS_IIDX_WSUB) {
            is_tdj_fhd = false;
        }

        use_native = nativetouch::is_hooked();

        // resolution the API/companion sends native touch coordinates in (after errata)
        native_canvas_w = 0;
        native_canvas_h = 0;
        if (is_sdvx) {
            // exceed gear subscreen, portrait after the rotation applied in apply_touch_errata
            native_canvas_w = 1080;
            native_canvas_h = 1920;
        } else if (avs::game::is_model("LDJ")) {
            // TDJ subscreen; FHD models are upscaled to 1080p by apply_touch_errata
            native_canvas_w = is_tdj_fhd ? 1920 : 1280;
            native_canvas_h = is_tdj_fhd ? 1080 : 720;
        } else if (avs::game::is_model("M39")) {
            // pop'n music API touch surface
            native_canvas_w = 1280;
            native_canvas_h = 800;
        }

        functions["read"] = std::bind(&Touch::read, this, _1, _2);
        functions["write"] = std::bind(&Touch::write, this, _1, _2);
        functions["write_reset"] = std::bind(&Touch::write_reset, this, _1, _2);
    }

    /**
     * read()
     */
    void Touch::read(api::Request &req, Response &res) {

        // get touch points
        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points);

        // add state for each touch point
        for (auto &touch : touch_points) {
            Value state(kArrayType);
            Value id((uint64_t) touch.id);
            Value x((int64_t) touch.x);
            Value y((int64_t) touch.y);
            Value mouse((bool) touch.mouse);
            state.PushBack(id, res.doc()->GetAllocator());
            state.PushBack(x, res.doc()->GetAllocator());
            state.PushBack(y, res.doc()->GetAllocator());
            state.PushBack(mouse, res.doc()->GetAllocator());
            res.add_data(state);
        }
    }

    /**
     * write([id: uint, x: int, y: int], ...)
     */
    void Touch::write(Request &req, Response &res) {

        // get all touch points
        std::vector<TouchPoint> touch_points;
        for (Value &param : req.params.GetArray()) {

            // check params
            if (param.Size() < 3) {
                error_params_insufficient(res);
                continue;
            }
            if (!param[0].IsUint()) {
                error_type(res, "id", "uint");
                continue;
            }
            if (!param[1].IsInt()) {
                error_type(res, "x", "int");
                continue;
            }
            if (!param[2].IsInt()) {
                error_type(res, "y", "int");
                continue;
            }
            // TODO: optional mouse parameter

            // get params
            auto touch_id = param[0].GetUint();
            auto touch_x = param[1].GetInt();
            auto touch_y = param[2].GetInt();

            apply_touch_errata(touch_x, touch_y);

            touch_points.emplace_back(TouchPoint {
                .id = touch_id,
                .x = touch_x,
                .y = touch_y,
                .mouse = false,
            });
        }

        // apply touch points
        if (use_native) {
            if (!touch_points.empty()) {
                inject_native_touch(touch_points, native_canvas_w, native_canvas_h);
            }
        } else {
            touch_write_points(&touch_points);
        }
    }

    /**
     * write_reset(id: uint, ...)
     */
    void Touch::write_reset(Request &req, Response &res) {

        // get all IDs
        std::vector<DWORD> touch_point_ids;
        for (Value &param : req.params.GetArray()) {

            // check params
            if (!param.IsUint()) {
                error_type(res, "id", "uint");
                continue;
            }

            // remember touch ID
            auto touch_id = param.GetUint();
            touch_point_ids.emplace_back(touch_id);
        }

        // remove all IDs
        if (use_native) {
            // native injection tracks a single contact, so IDs are ignored and any reset releases it
            inject_native_touch({}, native_canvas_w, native_canvas_h);
        } else {
            touch_remove_points(&touch_point_ids);
        }
    }

    void Touch::apply_touch_errata(int &x, int &y) {
        int x_raw = x;
        int y_raw = y;

        if (is_tdj_fhd) {
            // deal with TDJ FHD resolution mismatch (upgrade 720p to 1080p)
            // we don't know what screen is being shown on the companion and the API doesn't specify
            // the target of the touch events so just assume it's the sub screen
            x = x_raw * 1920 / 1280;
            y = y_raw * 1080 / 720;
        } else if (is_sdvx) {
            // for exceed gear, they are both 1080p screens, but need to apply transformation
            x = 1080 - y_raw;
            y = x_raw;
        }
    }
}
