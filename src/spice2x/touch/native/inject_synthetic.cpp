// enable Windows 8 touch injection types; the functions are loaded dynamically
#define _WIN32_WINNT 0x0602

#include <mutex>

#include <windows.h>
#include <windowsx.h>

#include "inject.h"
#include "inject_internal.h"
#include "settings.h"
#include "transform.h"

#include "util/logging.h"

namespace nativetouch::inject {

    constexpr UINT SYNTHETIC_CONTACT_TIMEOUT_MS = 100;

    enum class SyntheticTouchMessage : WPARAM {
        Up, // used by callers releasing a contact
        DownGameSpace, // coordinates are relative to the game's logical touch surface
        DownScreenSpace, // coordinates are absolute pixels in Windows desktop coordinates
    };

    static std::once_flag synthetic_initialization_once;
    static int synthetic_contact_timer_token;
    static UINT synthetic_touch_message;

    void initialize_synthetic_touch() {
        std::call_once(synthetic_initialization_once, [] {
            synthetic_touch_message = RegisterWindowMessageW(L"spice2x.native_touch.inject");
            if (synthetic_touch_message == 0) {
                log_warning(
                    "touch::native", "failed to register synthetic touch message: {}", GetLastError());
            }
        });
    }

    // synthetic touches preempt the mouse and keep it disabled until release or timeout
    static void begin_synthetic_contact(HWND window, POINT position, bool screen_space) {
        // remember when Windows-returned coordinates must map back into game space
        const auto transform_returned_coordinates =
            transform::is_tdj_dedicated_subscreen(window) ||
            settings::SYNTHETIC_TOUCH_USES_CLIENT_COORDINATES;
        if (!screen_space && !transform::game_to_screen(window, &position)) {
            return;
        }

        const auto timer_id = reinterpret_cast<UINT_PTR>(&synthetic_contact_timer_token);

        // when this producer already owns the contact, move it instead of releasing and
        // re-pressing so continuous input (such as the API surface) drags smoothly
        if (contact_is_owned_by(ContactOwner::Synthetic, window)) {
            if (update_contact(ContactOwner::Synthetic, window, position)) {
                // refresh the safety timeout while updates keep arriving
                if (SetTimer(window, timer_id, SYNTHETIC_CONTACT_TIMEOUT_MS, nullptr)) {
                    set_contact_timer(ContactOwner::Synthetic, window, timer_id);
                }
                return;
            }
        }

        if (!release_active_contact()) {
            return;
        }
        if (!begin_contact(
                ContactOwner::Synthetic,
                window,
                position,
                transform_returned_coordinates)) {
            return;
        }

        if (SetTimer(window, timer_id, SYNTHETIC_CONTACT_TIMEOUT_MS, nullptr)) {
            set_contact_timer(ContactOwner::Synthetic, window, timer_id);
        } else {
            log_warning("touch::native", "failed to start synthetic touch timeout timer");
        }
    }

    static void end_synthetic_contact(HWND window) {
        if (contact_is_owned_by(ContactOwner::Synthetic, window)) {
            release_active_contact();
        }
    }

    bool handle_synthetic_message(
            HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
        if (synthetic_touch_message != 0 && message == synthetic_touch_message) {
            POINT position { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
            switch (static_cast<SyntheticTouchMessage>(w_param)) {
                case SyntheticTouchMessage::DownGameSpace:
                    begin_synthetic_contact(window, position, false);
                    break;
                case SyntheticTouchMessage::DownScreenSpace:
                    begin_synthetic_contact(window, position, true);
                    break;
                default:
                    end_synthetic_contact(window);
                    break;
            }
            return true;
        }

        if (message == WM_TIMER &&
            w_param == reinterpret_cast<UINT_PTR>(&synthetic_contact_timer_token)) {
            end_synthetic_contact(window);
            return true;
        }

        return false;
    }

    static HWND prepare_synthetic_touch() {
        initialize_touch_injection();

        const auto window = get_injection_window();
        if (!touch_injection_available() || window == nullptr ||
            synthetic_touch_message == 0) {
            return nullptr;
        }
        return window;
    }

    static bool post_synthetic_touch(
            HWND window, POINT position, SyntheticTouchMessage message) {
        return PostMessageW(
            window,
            synthetic_touch_message,
            static_cast<WPARAM>(message),
            MAKELPARAM(position.x, position.y)) != FALSE;
    }

    // inject a point expressed in the game's synthetic touch coordinate space
    bool inject_synthetic_touch(POINT position, bool down) {
        const auto window = prepare_synthetic_touch();
        if (window == nullptr) {
            return false;
        }

        const auto message = down
            ? SyntheticTouchMessage::DownGameSpace
            : SyntheticTouchMessage::Up;
        return post_synthetic_touch(window, position, message);
    }

    // map a logical canvas point onto the live injection window before injecting it
    bool inject_synthetic_touch_from_canvas(POINT position, SIZE canvas, bool down) {
        const auto window = prepare_synthetic_touch();
        if (window == nullptr) {
            return false;
        }

        if (!down) {
            return post_synthetic_touch(window, position, SyntheticTouchMessage::Up);
        }

        RECT client_rect {};
        if (canvas.cx <= 0 || canvas.cy <= 0 ||
                !GetClientRect(window, &client_rect) ||
                client_rect.right <= 0 || client_rect.bottom <= 0) {
            return false;
        }

        position.x = MulDiv(position.x, client_rect.right, canvas.cx);
        position.y = MulDiv(position.y, client_rect.bottom, canvas.cy);
        if (!ClientToScreen(window, &position)) {
            return false;
        }

        return post_synthetic_touch(window, position, SyntheticTouchMessage::DownScreenSpace);
    }
}
