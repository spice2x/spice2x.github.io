// enable touch functions - set version to windows 7
// mingw otherwise doesn't load touch stuff
#define _WIN32_WINNT 0x0601

#include "wintouchemu.h"

#include "touch/touch.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/utils.h"

#define TOUCH_SIMULATE_FAT_FINGERS 0
#define TOUCH_DEBUG_VERBOSE 0

#if TOUCH_DEBUG_VERBOSE
#define log_debug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_debug(module, format_str, ...)
#endif

namespace nativetouchhook {

    static decltype(GetTouchInputInfo) *GetTouchInputInfo_orig = nullptr;

    static BOOL WINAPI GetTouchInputInfoHook(
        HTOUCHINPUT hTouchInput, UINT cInputs, PTOUCHINPUT pInputs, int cbSize) {

        // call the original fist
        const auto result = GetTouchInputInfo_orig(hTouchInput, cInputs, pInputs, cbSize);
        if (result == 0) {
            return result;
        }

        for (size_t i = 0; i < cInputs; i++) {
            PTOUCHINPUT point = &pInputs[i];

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
        
        return result;
    }

    void hook(HMODULE module) {
        GetTouchInputInfo_orig = detour::iat_try("GetTouchInputInfo", GetTouchInputInfoHook, module);
        if (GetTouchInputInfo_orig != nullptr) {
            log_misc("touch::native", "GetTouchInputInfo hooked");
        }
    }

}
 