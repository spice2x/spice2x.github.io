#include "la9a.h"

#include "games/pcm/io.h"
#include "hooks/graphics/graphics.h"
#include "touch/touch.h"
#include "util/utils.h"

#ifdef max
#undef max
#endif

namespace acio {

#pragma pack(push, 1)
    struct la9a_control_status {
        uint8_t p1 : 6;
        uint8_t service_button : 1;
        uint8_t test_button : 1;
        uint8_t p2[9];
        uint8_t lcd_counter;
        uint8_t p3[5];
        uint16_t touch_x;
        uint16_t touch_y;
        uint16_t touch_z;
        uint8_t p4[26];
    };
#pragma pack(pop)

    static struct la9a_control_status CONTROL_STATUS {};
    static bool TOUCH_ATTACHED = false;

    static bool __cdecl ac_io_la9a_set_error_message(int, unsigned int, int) {
        return true;
    }

    static bool __cdecl ac_io_la9a_update_control_status_buffer() {
        CONTROL_STATUS.touch_z = 0xFF;
        CONTROL_STATUS.test_button = 0;
        CONTROL_STATUS.service_button = 0;

        // attach touch handler on the first call to this function
        if (!TOUCH_ATTACHED) {
            log_misc("la9a", "attach touch handler");

            HWND hwnd = FindWindowBeginsWith("LA9");
            if (!hwnd) {
                log_fatal("la9a", "LA9 window not found");
            }
            touch_create_wnd(hwnd);
            graphics_hook_window(hwnd, nullptr);

            if (GRAPHICS_SHOW_CURSOR) {
                ShowCursor(1);
            }

            TOUCH_ATTACHED = true;
        }

        // update touch
        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points);

        if (!touch_points.empty()) {
            auto &touch_point = touch_points[0];

            // TODO: `x` and `y` should be clamped [0, std::numeric_limits<uint16_t>::max())
            CONTROL_STATUS.touch_x = static_cast<uint16_t>(touch_point.x);
            CONTROL_STATUS.touch_y = static_cast<uint16_t>(touch_point.y);
            CONTROL_STATUS.touch_z = 0;
        }
        CONTROL_STATUS.lcd_counter++;

        // update buttons
        auto &buttons = games::pcm::get_buttons();
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::pcm::Buttons::Test])) {
            CONTROL_STATUS.test_button = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::pcm::Buttons::Service])) {
            CONTROL_STATUS.service_button = 1;
        }

        return true;
    }

    static int __cdecl ac_io_la9a_update_counter(int, int) {
        return 0;
    }

    static int __cdecl ac_io_la9a_update_lcd(int) {
        return 1;
    }

    static void __cdecl ac_io_la9a_get_control_status_buffer(struct la9a_control_status *control_status) {
        *control_status = CONTROL_STATUS;
    }

    LA9AModule::LA9AModule(HMODULE module, HookMode hookMode) : ACIOModule("LA9A", module, hookMode) {
        //this->status_buffer = STATUS_BUFFER;
        //this->status_buffer_size = sizeof(STATUS_BUFFER);
        //this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
    }
    
    void LA9AModule::attach() {
        ACIOModule::attach();

        // hooks
        ACIO_MODULE_HOOK(ac_io_la9a_set_error_message);
        ACIO_MODULE_HOOK(ac_io_la9a_update_control_status_buffer);
        ACIO_MODULE_HOOK(ac_io_la9a_get_control_status_buffer);
        ACIO_MODULE_HOOK(ac_io_la9a_update_lcd);
        ACIO_MODULE_HOOK(ac_io_la9a_update_counter);
    }
}
