// enable touch functions - set version to windows 7
// mingw otherwise doesn't load touch stuff
#define _WIN32_WINNT 0x0601

#include "avs/game.h"
#include "rawinput/touch.h"
#include "inject.h"
#include "transform.h"

#include "util/detour.h"
#include "util/logging.h"

#define TOUCH_SIMULATE_FAT_FINGERS 0
#define TOUCH_DEBUG_VERBOSE 0

#if TOUCH_DEBUG_VERBOSE
#define log_debug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_debug(module, format_str, ...)
#endif

namespace nativetouch {

    static decltype(GetTouchInputInfo) *GetTouchInputInfo_orig = nullptr;
    static bool native_display_initialized = false;
    static DWORD native_display_orientation = DMDO_DEFAULT;
    static long native_display_size_x = 1920L;
    static long native_display_size_y = 1080L;

    static void update_native_display_mode() {
        RECT display_rect{};
        if (GetWindowRect(GetDesktopWindow(), &display_rect)) {
            native_display_size_x = display_rect.right - display_rect.left;
            native_display_size_y = display_rect.bottom - display_rect.top;
        }

        DEVMODE display_mode{};
        display_mode.dmSize = sizeof(display_mode);
        if (EnumDisplaySettingsEx(nullptr, ENUM_CURRENT_SETTINGS, &display_mode, EDS_RAWMODE) &&
            (display_mode.dmFields & DM_DISPLAYORIENTATION)) {
            native_display_orientation = display_mode.dmDisplayOrientation;
        } else {
            log_info("touch::native", "failed to determine monitor orientation");
        }

        log_info(
            "touch::native", "primary display mode: {}x{}, orientation {}",
            native_display_size_x,
            native_display_size_y,
            native_display_orientation);
    }

    static void strip_contact_size(PTOUCHINPUT point) {

#if TOUCH_SIMULATE_FAT_FINGERS
        point->dwMask |= 0x004;
        point->cxContact = 80 * 100;
        point->cyContact = 60 * 100;
#endif

        // most monitors do not set TOUCHEVENTFMASK_CONTACTAREA, but for
        // monitors that do set it, IIDX can get very confused (SDVX is not
        // affected)
        //
        // while the test menu and the touch "glow" seem to work properly,
        // interacting with subscreen menu items or entering PIN becomes
        // very unpredictable
        //
        // to fix this, simply remove the contact area width and height
        // 
        // note: test menu > I/O > touch test  gives 5 numbers:
        //       n: x, y, w, h
        //       where 
        //       n is the nth touch input since boot
        //       x, y are coordinates (center of finger)
        //       w, h are contact width and height
        //
        // when TOUCHEVENTFMASK_CONTACTAREA is not set, w/h will
        // automatically be seen as 1x1, which works perfectly fine

        log_debug(
            "touch::native",
            "[{}, {}] dwMask = 0x{:x}, cxContact = {}, cyContact = {}",
            point->x / 100,
            point->y / 100,
            point->dwMask,
            point->cxContact,
            point->cyContact);

        point->dwMask &= ~(0x004ul); // clear TOUCHEVENTFMASK_CONTACTAREA 
        point->cxContact = 0;
        point->cyContact = 0;
    }

    static void flip_touch_points(PTOUCHINPUT point) {
        point->x = native_display_size_x * 100 - point->x;
        point->y = native_display_size_y * 100 - point->y;
    }

    static BOOL WINAPI GetTouchInputInfoHook(
        HTOUCHINPUT hTouchInput, UINT cInputs, PTOUCHINPUT pInputs, int cbSize) {

        // Refresh after exclusive fullscreen establishes the final display mode.
        if (!native_display_initialized) {
            update_native_display_mode();
            native_display_initialized = true;
        }

        // call the original fist
        const auto result = GetTouchInputInfo_orig(hTouchInput, cInputs, pInputs, cbSize);
        if (result == 0) {
            return result;
        }

        bool flip_hardware_touch = false;
        if (avs::game::is_model("KFC")) {
            log_debug(
                "touch::native", "orientation = {}, display size = {}x{}",
                native_display_orientation,
                native_display_size_x,
                native_display_size_y);
            if (native_display_orientation == DMDO_270) {
                flip_hardware_touch = true;
            }
        }

        for (size_t i = 0; i < cInputs; i++) {
            PTOUCHINPUT point = &pInputs[i];

            const auto synthetic = inject::transform_touch_input(point);

            if (avs::game::is_model("LDJ")) {
                strip_contact_size(point);
            }

            const auto flip_values = !synthetic &&
                (rawinput::touch::INVERTED ^ flip_hardware_touch);
            if (flip_values) {
                flip_touch_points(point);
            }
            if (!synthetic) {
                POINT position { point->x / 100, point->y / 100 };
                const auto transform_result =
                    transform::hardware_to_game(&position);
                if (transform_result == transform::Result::Transformed) {
                    point->x = position.x * 100;
                    point->y = position.y * 100;
                } else if (transform_result == transform::Result::Rejected &&
                    !(point->dwFlags & TOUCHEVENTF_UP)) {
                    // suppress rejected contacts, but preserve UP to release an active touch ID
                    point->dwFlags = 0;
                }
            }
        }
        
        return result;
    }

    void hook(HMODULE module) {
        inject::hook(module);

        GetTouchInputInfo_orig = detour::iat_try("GetTouchInputInfo", GetTouchInputInfoHook, module);
        if (GetTouchInputInfo_orig != nullptr) {
            log_misc("touch::native", "GetTouchInputInfo hooked");
        }
    }

}
 