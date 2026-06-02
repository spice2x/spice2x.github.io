#pragma once

#include <string>

#include <windows.h>

namespace games::gitadora {

    void configure_arena_render_resolution();
    bool is_arena_resolution_patch_enabled();
    void apply_arena_resolution_patch();
    void apply_live2d_disable_patch();
    void apply_arena_fullscreen_window_size(
            const std::string &window_name,
            int &x,
            int &y,
            int &width,
            int &height);
    void scale_arena_fullscreen_touch_point(LONG &x, LONG &y);
}
