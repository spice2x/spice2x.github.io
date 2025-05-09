#include "loveplus.h"

#include <windows.h>

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "hooks/devicehook.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

namespace games::loveplus {

    bool CAMERA_ENABLE = false;

    // touch stuff
    static bool TOUCH_ENABLE = false;
    static bool TOUCH_ATTACHED = false;

    static std::string lp_args = "-noWatchDog -noIOError -noIrda -notarget";

    void touch_update() {

        // check if touch enabled
        if (!TOUCH_ENABLE) {
            return;
        }

        // attach touch module
        if (!TOUCH_ATTACHED) {

            // Find the game window.
            HWND wnd = FindProcessWindowBeginsWith("LovePlusAC");

            // attach touch hook
            if (wnd) {
                log_info(
                    "loveplus",
                    "using window handle for touch: {} ({})",
                    fmt::ptr(wnd),
                    get_window_title(wnd));
                touch_create_wnd(wnd);
            } else {
                log_info("loveplus", "falling back to the DirectX window handle for touch");
                touch_attach_dx_hook();
            }

            // show cursor
            if (GRAPHICS_SHOW_CURSOR) {
                ShowCursor(1);
            }

            // set attached
            TOUCH_ATTACHED = true;
        }
    }

    static int __cdecl lib_touchpanel_init() {
        touch_update();

        return 0;
    }

    static int __cdecl lib_touchpanel_calibrate(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
        return 0;
    }

    // FIXME: test this calling convention change!
    static int __cdecl lib_touchpanel_diagnostics(void *a1) {
        return 0;
    }

    static int __cdecl lib_touchpanel_get_touch(DWORD *a1) {
        touch_update();

        // get touch points
        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points);

        // check first touch point
        if (!touch_points.empty()) {
            auto &tp = touch_points[0];

            a1[0] = tp.y;
            a1[1] = tp.x;
            a1[2] = 0;
            a1[3] = 1;
            a1[4] = 0;
        } else {
            a1[0] = 0;
            a1[1] = 0;
            a1[2] = 0;
            a1[3] = 0;
            a1[4] = 0;
        }

        return 0;
    }

    static int __cdecl lib_touchpanel_get_touch_direct(DWORD *a1) {
        return lib_touchpanel_get_touch(a1);
    }

    static int __cdecl lib_touchpanel_update() {
        return 1;
    }

    static bool __cdecl lib_touchpanel_term() {
        return 0;
    }

    static bool __cdecl lib_touchpanel_instance_term() {
        return 0;
    }

    static BOOL __cdecl SetEqualizer(char *a1) {
        return FALSE;
    }

    static LPSTR __stdcall GetCommandLineA_hook() {
        return lp_args.data();
    }

    LovePlusGame::LovePlusGame() : Game("LovePlus") {
    }

    void LovePlusGame::attach() {
        Game::attach();

        // init stuff
        devicehook_init();

        // enable touch
        TOUCH_ENABLE = true;

        // touchpanel emulation
        HMODULE lib_touchpanel = libutils::try_library("lib_touchpanel.dll");
        detour::inline_hook((void *) lib_touchpanel_init, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_init"));
        detour::inline_hook((void *) lib_touchpanel_calibrate, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_calibrate"));
        detour::inline_hook((void *) lib_touchpanel_diagnostics, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_diagnostics"));
        detour::inline_hook((void *) lib_touchpanel_get_touch, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_get_touch_direct"));
        detour::inline_hook((void *) lib_touchpanel_get_touch_direct, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_get_touch"));
        detour::inline_hook((void *) lib_touchpanel_update, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_update"));
        detour::inline_hook((void *) lib_touchpanel_term, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_term"));
        detour::inline_hook((void *) lib_touchpanel_instance_term, libutils::try_proc(
                  lib_touchpanel, "lib_touchpanel_instance_term"));

        // equalizer crash fix
        HMODULE seteq = libutils::try_library("ad_hd_seteq_dll.dll");
        detour::inline_hook((void *) SetEqualizer, libutils::try_proc(seteq, "SetEqualizer"));

        // set up command line args
        if (!CAMERA_ENABLE) {
            lp_args += " -noCamera";
        }
        if (GRAPHICS_WINDOWED) {
            lp_args += " -win";
        }
        // get command line hook
        HMODULE lpac = libutils::try_library("lpac.dll");
        detour::iat("GetCommandLineA", GetCommandLineA_hook, lpac);
    }

    void LovePlusGame::detach() {
        Game::detach();

        // disable touch
        TOUCH_ENABLE = false;
    }
}
