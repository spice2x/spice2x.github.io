#include "ezusb.h"

#include "cfg/api.h"
#include "cfg/button.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

#include "io.h"

namespace games::qma {

    static long DISPLAY_SIZE_X = 1360L;
    static long DISPLAY_SIZE_Y = 768L;

    std::array<bool, 39> TOUCH_STATUS = {};
    std::array<int, 39> TOUCH_X_COORD = {230, 277, 323, 369, 417, 463, 510, 555, 601, 647, 695, 250, 296, 344, 389, 435, 483, 528, 575, 621, 667, 259, 308, 353, 398, 444, 491, 537, 582, 638, 280, 324, 370, 422, 465, 510, 557, 760, 759};
    std::array<int, 39> TOUCH_Y_COORD = {642, 642, 642, 642, 642, 642, 642, 642, 642, 642, 642, 728, 728, 728, 728, 728, 728, 728, 728, 728, 728, 813, 813, 813, 813, 813, 813, 813, 813, 813, 894, 894, 894, 894, 894, 894, 894, 771, 886};

    static int __cdecl usbCheckAlive() {
        return 1;
    }

    static int __cdecl usbCheckSecurityNew() {
        return 0;
    }

    static int __cdecl usbCoinGet() {
        return eamuse_coin_get_stock();
    }

    static int __cdecl usbCoinMode() {
        return 0;
    }

    static int __cdecl usbEnd() {
        return 0;
    }

    static int __cdecl usbFirmResult() {
        return 0;
    }

    static int __cdecl usbGetKEYID() {
        return 0;
    }

    static int __cdecl usbGetSecurity() {
        return 0;
    }

    static int __cdecl usbLamp(uint32_t lamp_bits) {

        // get lights
        auto &lights = get_lights();

        // mapping
        static const size_t bits[] = {
                0x20, 0x40, 0x80,
                0x08, 0x10, 0x20
        };
        static const size_t vals[] = {
                Lights::LampRed, Lights::LampGreen, Lights::LampBlue,
                Lights::ButtonLeft, Lights::ButtonRight, Lights::ButtonOK
        };
        for (size_t i = 0; i < 3; i++) {
            float value = (lamp_bits & bits[i]) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(vals[i]), value);
        }

        // return no error
        return 0;
    }

    static int __cdecl usbPadRead(unsigned int *pad_bits) {

        // get buttons
        auto &buttons = get_buttons();

        // reset
        *pad_bits = 0;

        // mappings
        static const struct {
            size_t button, shift;
        } mappings[] = {
                { Buttons::Service, 19 },
                { Buttons::Test, 18 },
                { Buttons::Select, 17 },
                { Buttons::CoinMech, 16 },
                { Buttons::Select1, 0 },
                { Buttons::Select2, 1 },
                { Buttons::OK, 2 },
                { Buttons::Left, 6 },
                { Buttons::Right, 7 },
                { Buttons::TouchKey1, 100 },
                { Buttons::TouchKey2, 101 },
                { Buttons::TouchKey3, 102 },
                { Buttons::TouchKey4, 103 },
                { Buttons::TouchKey5, 104 },
                { Buttons::TouchKey6, 105 },
                { Buttons::TouchKey7, 106 },
                { Buttons::TouchKey8, 107 },
                { Buttons::TouchKey9, 108 },
                { Buttons::TouchKey0, 109 },
                { Buttons::TouchKeyDash, 110 },
                { Buttons::TouchKeyQ, 111 },
                { Buttons::TouchKeyW, 112 },
                { Buttons::TouchKeyE, 113 },
                { Buttons::TouchKeyR, 114 },
                { Buttons::TouchKeyT, 115 },
                { Buttons::TouchKeyY, 116 },
                { Buttons::TouchKeyU, 117 },
                { Buttons::TouchKeyI, 118 },
                { Buttons::TouchKeyO, 119 },
                { Buttons::TouchKeyP, 120 },
                { Buttons::TouchKeyA, 121 },
                { Buttons::TouchKeyS, 122 },
                { Buttons::TouchKeyD, 123 },
                { Buttons::TouchKeyF, 124 },
                { Buttons::TouchKeyG, 125 },
                { Buttons::TouchKeyH, 126 },
                { Buttons::TouchKeyJ, 127 },
                { Buttons::TouchKeyK, 128 },
                { Buttons::TouchKeyL, 129 },
                { Buttons::TouchKeyZ, 130 },
                { Buttons::TouchKeyX, 131 },
                { Buttons::TouchKeyC, 132 },
                { Buttons::TouchKeyV, 133 },
                { Buttons::TouchKeyB, 134 },
                { Buttons::TouchKeyN, 135 },
                { Buttons::TouchKeyM, 136 },
                { Buttons::TouchKeyBackspace, 137 },
                { Buttons::TouchKeyEnter, 138 }
        };

        // set buttons
        for (auto &mapping : mappings) {
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(mapping.button))) {
                if (mapping.shift > 99) {
                    if (!TOUCH_STATUS[mapping.shift - 100]) {

                        static RECT display_rect;
                        GetWindowRect(GetDesktopWindow(), &display_rect);
                        DISPLAY_SIZE_X = display_rect.right - display_rect.left;
                        DISPLAY_SIZE_Y = display_rect.bottom - display_rect.top;

                        std::vector<TouchPoint> touch_write;
                        std::vector<DWORD> touch_point_ids;
                        TouchPoint tp {
                            .id = 0,
                            .x = (long)(TOUCH_X_COORD[mapping.shift - 100] * DISPLAY_SIZE_X / 1000),
                            .y = (long)(TOUCH_Y_COORD[mapping.shift - 100] * DISPLAY_SIZE_Y / 1000),
                            .mouse = false,
                        };
                        TOUCH_STATUS[mapping.shift - 100] = true;
                        touch_write.emplace_back(tp);
                        touch_write_points(&touch_write);

                        touch_point_ids.emplace_back(0);
                        touch_remove_points(&touch_point_ids);
                    }
                    continue;
                } else {
                    *pad_bits |= 1 << mapping.shift;
                }
            } else {
                if (mapping.shift > 99) {
                    TOUCH_STATUS[mapping.shift - 100] = false;
                }
            }
        }
        // return no error
        return 0;
    }

    static int __cdecl usbPadReadLast(uint8_t *a1) {
        memset(a1, 0, 40);
        return 0;
    }

    static int __cdecl usbSecurityInit() {
        return 0;
    }

    static int __cdecl usbSecurityInitDone() {
        return 0;
    }

    static int __cdecl usbSecuritySearch() {
        return 0;
    }

    static int __cdecl usbSecuritySearchDone() {
        return 0;
    }

    static int __cdecl usbSecuritySelect() {
        return 0;
    }

    static int __cdecl usbSecuritySelectDone() {
        return 0;
    }

    static int __cdecl usbSetExtIo() {
        return 0;
    }

    static int __cdecl usbStart() {
        return 0;
    }

    static int __cdecl usbWdtReset() {
        return 0;
    }

    static int __cdecl usbWdtStart() {
        return 0;
    }

    static int __cdecl usbWdtStartDone() {
        return 0;
    }

    static int __cdecl usbCoinBlocker(int a1) {
        return 0;
    }

    static int __cdecl usbMute(int a1) {
        return 0;
    }

    static int __cdecl usbIsHiSpeed() {
        return 1;
    }

    void ezusb_init() {

        TOUCH_STATUS.fill(false);
        // load the library
        HINSTANCE ezusb = libutils::try_library("ezusb.dll");

        // insert hooks
        if (ezusb) {
            detour::inline_hook((void *) usbCheckAlive,
                                libutils::try_proc(ezusb, "?usbCheckAlive@@YAHXZ"));
            detour::inline_hook((void *) usbCheckSecurityNew,
                                libutils::try_proc(ezusb, "?usbCheckSecurityNew@@YAHH@Z"));
            detour::inline_hook((void *) usbCoinGet,
                                libutils::try_proc(ezusb, "?usbCoinGet@@YAHH@Z"));
            detour::inline_hook((void *) usbCoinMode,
                                libutils::try_proc(ezusb, "?usbCoinMode@@YAHH@Z"));
            detour::inline_hook((void *) usbEnd,
                                libutils::try_proc(ezusb, "?usbEnd@@YAHXZ"));
            detour::inline_hook((void *) usbFirmResult,
                                libutils::try_proc(ezusb, "?usbFirmResult@@YAHXZ"));
            detour::inline_hook((void *) usbGetKEYID,
                                libutils::try_proc(ezusb, "?usbGetKEYID@@YAHPAEH@Z"));
            detour::inline_hook((void *) usbGetSecurity,
                                libutils::try_proc(ezusb, "?usbGetSecurity@@YAHHPAE@Z"));
            detour::inline_hook((void *) usbLamp,
                                libutils::try_proc(ezusb, "?usbLamp@@YAHH@Z"));
            detour::inline_hook((void *) usbPadRead,
                                libutils::try_proc(ezusb, "?usbPadRead@@YAHPAK@Z"));
            detour::inline_hook((void *) usbPadReadLast,
                                libutils::try_proc(ezusb, "?usbPadReadLast@@YAHPAE@Z"));
            detour::inline_hook((void *) usbSecurityInit,
                                libutils::try_proc(ezusb, "?usbSecurityInit@@YAHXZ"));
            detour::inline_hook((void *) usbSecurityInitDone,
                                libutils::try_proc(ezusb, "?usbSecurityInitDone@@YAHXZ"));
            detour::inline_hook((void *) usbSecuritySearch,
                                libutils::try_proc(ezusb, "?usbSecuritySearch@@YAHXZ"));
            detour::inline_hook((void *) usbSecuritySearchDone,
                                libutils::try_proc(ezusb, "?usbSecuritySearchDone@@YAHXZ"));
            detour::inline_hook((void *) usbSecuritySelect,
                                libutils::try_proc(ezusb, "?usbSecuritySelect@@YAHH@Z"));
            detour::inline_hook((void *) usbSecuritySelectDone,
                                libutils::try_proc(ezusb, "?usbSecuritySelectDone@@YAHXZ"));
            detour::inline_hook((void *) usbSetExtIo,
                                libutils::try_proc(ezusb, "?usbSetExtIo@@YAHH@Z"));
            detour::inline_hook((void *) usbStart,
                                libutils::try_proc(ezusb, "?usbStart@@YAHH@Z"));
            detour::inline_hook((void *) usbWdtReset,
                                libutils::try_proc(ezusb, "?usbWdtReset@@YAHXZ"));
            detour::inline_hook((void *) usbWdtStart,
                                libutils::try_proc(ezusb, "?usbWdtStart@@YAHH@Z"));
            detour::inline_hook((void *) usbWdtStartDone,
                                libutils::try_proc(ezusb, "?usbWdtStartDone@@YAHXZ"));
            detour::inline_hook((void *) usbCoinBlocker,
                                libutils::try_proc(ezusb, "?usbCoinBlocker@@YAHH@Z"));
            detour::inline_hook((void *) usbMute,
                                libutils::try_proc(ezusb, "?usbMute@@YAHH@Z"));
            detour::inline_hook((void *) usbIsHiSpeed,
                                libutils::try_proc(ezusb, "?usbIsHiSpeed@@YAHXZ"));
        }
    }
}
