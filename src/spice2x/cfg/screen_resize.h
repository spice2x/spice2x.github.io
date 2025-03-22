#pragma once

#include <memory>
#include <string>
#include <optional>
#include <filesystem>
#include "external/rapidjson/document.h"

namespace cfg {

    enum WindowDecorationMode {
        Default = 0,
        Borderless = 1,
        ResizableFrame = 2
    };

    struct fullscreen_setting {
        int offset_x = 0;
        int offset_y = 0;
        float scale_x = 1.0;
        float scale_y = 1.0;
        bool keep_aspect_ratio = true;
        bool centered = true;
    };

    extern std::optional<std::string> SCREEN_RESIZE_CFG_PATH_OVERRIDE;

    class ScreenResize {
    private:
        std::filesystem::path config_path;
        // bool config_dirty = false;

        bool load_bool_value(rapidjson::Document& doc, std::string path, bool& value);
        bool load_int_value(rapidjson::Document& doc, std::string path, int& value);
        bool load_uint32_value(rapidjson::Document& doc, std::string path, uint32_t& value);
        bool load_float_value(rapidjson::Document& doc, std::string path, float& value);

    public:
        ScreenResize();
        ~ScreenResize();
        
        // full screen (directx) image settings
        bool enable_screen_resize = false;
        int8_t screen_resize_current_scene = 0;
        bool enable_linear_filter = true;
        fullscreen_setting scene_settings[4];

        // windowed mode sizing
        // Windows terminology:
        //     window = rectangle including the frame
        //     client = just the content area without frames.
        bool window_always_on_top = false;
        bool client_keep_aspect_ratio = true;
        bool enable_window_resize = false;
        int window_decoration = 0; // enum type WindowDecorationMode
        uint32_t client_width = 0;
        uint32_t client_height = 0;
        int32_t window_offset_x = 0;
        int32_t window_offset_y = 0;

        // these are not saved by config, but used by window management
        uint32_t init_client_width = 0;
        uint32_t init_client_height = 0;
        float init_client_aspect_ratio = 1.f;
        uint32_t init_window_style = 0;
        uint32_t window_deco_width = 0;
        uint32_t window_deco_height = 0;

        void config_load();
        void config_save();
    };

    // globals
    extern std::unique_ptr<cfg::ScreenResize> SCREENRESIZE;
}
