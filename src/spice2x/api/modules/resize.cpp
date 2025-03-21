#include "resize.h"
#include "external/rapidjson/document.h"
#include "cfg/screen_resize.h"

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    static thread_local std::vector<uint8_t> CAPTURE_BUFFER;

    Resize::Resize() : Module("resize") {
        functions["image_resize_enable"] = std::bind(&Resize::image_resize_enable, this, _1, _2);
        functions["image_resize_set_scene"] = std::bind(&Resize::image_resize_set_scene, this, _1, _2);
    }

    /**
     * image_resize_enable(enable: bool)
     */
    void Resize::image_resize_enable(Request &req, Response &res) {
        if (req.params.Size() < 1) {
            return error_params_insufficient(res);
        }
        if (!req.params[0].IsBool()) {
            return error_type(res, "enable", "bool");
        }

        cfg::SCREENRESIZE->enable_screen_resize = req.params[0].GetBool();
    }

    /**
     * image_resize_set_scene(scene: int)
     */
    void Resize::image_resize_set_scene(Request &req, Response &res) {
        if (req.params.Size() < 1) {
            return error_params_insufficient(res);
        }
        if (!req.params[0].IsInt()) {
            return error_type(res, "scene", "int");
        }

        const auto scene = req.params[0].GetInt();
        if (scene < 0 || (int)std::size(cfg::SCREENRESIZE->scene_settings) < scene) {
            return error(res, "invalid scene number");
        }
        if (scene == 0) {
            cfg::SCREENRESIZE->enable_screen_resize = false;
        } else {
            cfg::SCREENRESIZE->enable_screen_resize = true;
            cfg::SCREENRESIZE->screen_resize_current_scene = scene - 1;
        }
    }
}
