#include "unity_player.h"
#include "cfg/screen_resize.h"
#include "hooks/graphics/graphics.h"
#include "external/fmt/include/fmt/format.h"

namespace unity_utils {

std::string get_unity_player_args() {
    std::string args = "";
    // windowed
    if (GRAPHICS_WINDOWED) {
        args += " -screen-fullscreen 0";

        // window size - by default unity player will attempt to create a window that fills the
        // screen, so instead fall back to 1080p resolution
        uint32_t w = 1920;
        uint32_t h = 1080;
        if (GRAPHICS_WINDOW_SIZE.has_value()) {
            w = GRAPHICS_WINDOW_SIZE.value().first;
            h = GRAPHICS_WINDOW_SIZE.value().second;
        }
        args += fmt::format(" -screen-width {} -screen-height {}", w, h);

        // window border
        // eventually we should launch the player inside a parent window that we have full control
        // over using -parentHWND so we can let the user resize it by dragging the border...
        if (GRAPHICS_WINDOW_STYLE.has_value()) {
            if (GRAPHICS_WINDOW_STYLE == cfg::WindowDecorationMode::Borderless) {
                args += " -popupwindow";
            }
        }
    } else {
        // need to specify this, otherwise it gets cached and uses previous value
        args += " -screen-fullscreen 1";
    }

    // monitor
    if (D3D9_ADAPTER.has_value()) {
        args += fmt::format(" -monitor {}", D3D9_ADAPTER.value());
    }

    return args;
}

}