#include "touchpanel.h"

#include <cstdint>
#include "hooks/graphics/graphics.h"
#include "util/detour.h"
#include "util/time.h"

namespace games::we {

    struct TouchPanel {
        int width, height;
        uint8_t com_port;
        uint64_t wait_receive_time;
    };

    static TouchPanel* __cdecl ac_touchpanel_new2(int width, int height, uint8_t com_port) {
        auto tp = new TouchPanel();
        tp->width = width;
        tp->height = height;
        tp->com_port = com_port;
        tp->wait_receive_time = 0;
        return tp;
    }

    static bool __cdecl ac_touchpanel_polling(TouchPanel *tp) {
        return true;
    }

    static bool __cdecl ac_touchpanel_set_keepalive_mode(TouchPanel *tp, uint8_t a2, int a3, int a4) {
        return true;
    }

    static bool __cdecl ac_touchpanel_renewal(TouchPanel *tp) {
        return true;
    }

    static bool __cdecl ac_touchpanel_check_wait_receive(TouchPanel *tp) {
        if (tp) {
            return get_system_milliseconds() >= tp->wait_receive_time;
        }
        return false;
    }

    static bool __cdecl ac_touchpanel_delete(TouchPanel *tp) {
        if (tp) {
            delete tp;
            return true;
        }
        return false;
    }

    static bool __cdecl ac_touchpanel_get_keepalive_error(TouchPanel *tp) {
        return false;
    }

    static bool __cdecl ac_touchpanel_receive_check_diag(TouchPanel *tp, uint8_t *value) {
        *value = 1;
        return true;
    }

    static bool __cdecl ac_touchpanel_caribrate_set(TouchPanel *tp, int a2, int a3, int a4, int a5,
            int width, int height, int a8, int a9) {
        if (tp) {
            tp->width = width;
            tp->height = height;
            return true;
        }
        return false;
    }

    static bool __cdecl ac_touchpanel_send_check_diag(TouchPanel *tp) {
        if (tp) {
            tp->wait_receive_time = get_system_milliseconds() + 128;
            return true;
        }
        return false;
    }

    static void ac_touchpanel_translate(LPPOINT lpPoint) {

        // try to find game window - the second window without title
        static HWND target = 0;
        if (target == 0) {
            bool skip = false;
            for (auto &hWnd : GRAPHICS_WINDOWS) {
                char buffer[2] {};
                auto len = GetWindowTextA(hWnd, buffer, sizeof(buffer));
                if (len == 0) {
                    if (skip) {
                        target = hWnd;
                        break;
                    } else {
                        skip = true;
                    }
                }
            }
        }

        // translate screen coordinates to game window
        if (target) {
            ScreenToClient(target, lpPoint);
        }
    }

    static bool __cdecl ac_touchpanel_read_on(TouchPanel *tp, int *x, int *y, int *z, bool *down) {
        POINT pt {};
        GetCursorPos(&pt);
        ac_touchpanel_translate(&pt);
        *x = pt.x;
        *y = pt.y;
        *z = 0xFF;
        *down = GetKeyState(VK_LBUTTON) != 0;
        return true;
    }

    static bool __cdecl ac_touchpanel_read_on_direct(TouchPanel *tp, int *x, int *y, int *z, bool *down) {
        POINT pt {};
        GetCursorPos(&pt);
        ac_touchpanel_translate(&pt);
        *x = pt.x;
        *y = pt.y;
        *z = 0xFF;
        *down = GetKeyState(VK_LBUTTON) != 0;
        return true;
    }

    void touchpanel_init() {
        detour::iat_try("ac_touchpanel_new2", ac_touchpanel_new2);
        detour::iat_try("ac_touchpanel_polling", ac_touchpanel_polling);
        detour::iat_try("ac_touchpanel_set_keepalive_mode", ac_touchpanel_set_keepalive_mode);
        detour::iat_try("ac_touchpanel_renewal", ac_touchpanel_renewal);
        detour::iat_try("ac_touchpanel_check_wait_receive", ac_touchpanel_check_wait_receive);
        detour::iat_try("ac_touchpanel_delete", ac_touchpanel_delete);
        detour::iat_try("ac_touchpanel_get_keepalive_error", ac_touchpanel_get_keepalive_error);
        detour::iat_try("ac_touchpanel_receive_check_diag", ac_touchpanel_receive_check_diag);
        detour::iat_try("ac_touchpanel_caribrate_set", ac_touchpanel_caribrate_set);
        detour::iat_try("ac_touchpanel_send_check_diag", ac_touchpanel_send_check_diag);
        detour::iat_try("ac_touchpanel_read_on", ac_touchpanel_read_on);
        detour::iat_try("ac_touchpanel_read_on_direct", ac_touchpanel_read_on_direct);
    }
}
