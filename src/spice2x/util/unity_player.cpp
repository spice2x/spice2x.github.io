#include "unity_player.h"
#include "cfg/screen_resize.h"
#include "hooks/graphics/graphics.h"
#include "external/fmt/include/fmt/format.h"
#include "util/detour.h"
#include "util/logging.h"

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

    static std::string cmdLine;
    static decltype(GetCommandLineA) *GetCommandLineA_orig = nullptr;

    static LPSTR WINAPI GetCommandLineA_hook() {
        return (LPSTR) cmdLine.c_str();
    }

    void set_args(const std::string &args) {
        static bool init = false;
        if (!init) {
            init = true;
            detour::trampoline_try("kernel32.dll", "GetCommandLineA",
                                   (void*)GetCommandLineA_hook, (void**)&GetCommandLineA_orig);
        }

        cmdLine = args;
        log_info("unity", "unity player args: ```{}```", cmdLine);
    }

    static bool show = false;
    static decltype(ShowCursor) *ShowCursor_orig = nullptr;

    static int WINAPI ShowCursor_hook(BOOL bShow) {
        return show;
    }

    void force_show_cursor(bool bShow) {
        static bool init = false;
        if (!init) {
            init = true;
            detour::trampoline_try("user32.dll", "ShowCursor",
                                   (void*)ShowCursor_hook, (void**)&ShowCursor_orig);
        }

        show = bShow;
    }

}