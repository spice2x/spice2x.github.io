// enable Windows 8 touch injection types; the functions are loaded dynamically
#define _WIN32_WINNT 0x0602

#include <windows.h>

#include "inject_internal.h"
#include "transform.h"

#include "touch/touch.h"
#include "util/logging.h"

namespace nativetouch::inject {

    constexpr UINT CONTACT_TIMER_INTERVAL_MS = 16;

    static int mouse_contact_timer_token;

    struct PrimaryMouseButton {
        UINT down_message;
        UINT double_click_message;
        UINT up_message;
        WPARAM state_mask;
    };

    // honor the user's swapped-button setting when choosing the primary button
    static PrimaryMouseButton get_primary_mouse_button() {
        if (GetSystemMetrics(SM_SWAPBUTTON)) {
            return { WM_RBUTTONDOWN, WM_RBUTTONDBLCLK, WM_RBUTTONUP, MK_RBUTTON };
        }
        return { WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, WM_LBUTTONUP, MK_LBUTTON };
    }

    // use the current physical cursor but reject points outside the subscreen
    static bool get_mouse_injection_position(HWND window, POINT *position) {

        // queued WM_MOUSEMOVE coordinates can lag behind the cursor; injecting them makes
        // Windows move its primary pointer back to stale positions during a drag.
        if (!GetCursorPos(position)) {
            return false;
        }

        POINT transformed = *position;
        return transform::screen_to_game(window, &transformed);
    }

    // release the active injected contact and its window capture
    static void end_mouse_contact(HWND window) {
        if (contact_is_owned_by(ContactOwner::Mouse, window)) {
            release_active_contact();
        }
    }

    // begin a contact at the physical cursor position and capture future mouse input
    static void begin_mouse_contact(HWND window) {
        if (contact_is_active()) {
            return;
        }

        POINT position;
        if (!get_mouse_injection_position(window, &position) ||
            !begin_contact(ContactOwner::Mouse, window, position, true)) {
            return;
        }

        // keep receiving drag messages after the cursor leaves the client area
        SetCapture(window);
        const auto timer_id = reinterpret_cast<UINT_PTR>(&mouse_contact_timer_token);
        if (SetTimer(window, timer_id, CONTACT_TIMER_INTERVAL_MS, nullptr)) {
            set_contact_timer(ContactOwner::Mouse, window, timer_id);
        } else {
            log_warning("touch::native", "failed to start mouse touch injection timer");
        }
    }

    // update the contact while the primary button remains held
    static void move_mouse_contact(
            HWND window, WPARAM w_param, WPARAM primary_button_state) {
        if (!contact_is_owned_by(ContactOwner::Mouse, window)) {
            return;
        }

        POINT position;
        if (!get_mouse_injection_position(window, &position)) {
            end_mouse_contact(window);
            return;
        }

        if ((w_param & primary_button_state) == 0) {
            end_mouse_contact(window);
            return;
        }

        update_contact(ContactOwner::Mouse, window, position);
    }

    // emit stationary update frames so Windows keeps the contact alive
    static void refresh_mouse_contact(HWND window) {
        if (!contact_is_owned_by(ContactOwner::Mouse, window)) {
            return;
        }

        POINT position {};
        if (!GetCursorPos(&position)) {
            return;
        }

        POINT transformed = position;
        if (!transform::screen_to_game(window, &transformed)) {
            end_mouse_contact(window);
            return;
        }

        update_contact(ContactOwner::Mouse, window, position);
    }

    bool handle_mouse_message(HWND window, UINT message, WPARAM w_param) {
        if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST &&
            is_mouse_message_from_touchscreen()) {
            return true;
        }

        if (message == WM_TIMER &&
            w_param == reinterpret_cast<UINT_PTR>(&mouse_contact_timer_token)) {
            refresh_mouse_contact(window);
            return true;
        }

        const auto primary_button = get_primary_mouse_button();
        if (message == primary_button.down_message ||
            message == primary_button.double_click_message) {
            begin_mouse_contact(window);
        } else if (message == WM_MOUSEMOVE) {
            move_mouse_contact(window, w_param, primary_button.state_mask);
        } else if (message == primary_button.up_message) {
            end_mouse_contact(window);
        } else if (message == WM_CANCELMODE || message == WM_KILLFOCUS ||
                   message == WM_CAPTURECHANGED) {
            end_mouse_contact(window);
        }

        return false;
    }
}
