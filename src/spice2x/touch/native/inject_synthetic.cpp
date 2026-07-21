// enable Windows 8 touch injection types; the functions are loaded dynamically
#define _WIN32_WINNT 0x0602

#include <mutex>

#include <windows.h>
#include <windowsx.h>

#include "inject.h"
#include "inject_internal.h"

#include "touch/touch.h"
#include "util/logging.h"

namespace nativetouch_inject {

    namespace internal {

        constexpr UINT SYNTHETIC_CONTACT_TIMEOUT_MS = 100;

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

        // map a game-space synthetic contact onto the physical dedicated subscreen
        static bool transform_synthetic_position(HWND window, POINT *position) {
            if (!is_tdj_dedicated_subscreen(window)) {
                return true;
            }

            RECT client_rect {};
            if (!GetClientRect(window, &client_rect) ||
                client_rect.right <= 0 || client_rect.bottom <= 0 ||
                SPICETOUCH_TOUCH_WIDTH <= 0 || SPICETOUCH_TOUCH_HEIGHT <= 0) {
                return false;
            }

            position->x = MulDiv(
                position->x - SPICETOUCH_TOUCH_X,
                client_rect.right,
                SPICETOUCH_TOUCH_WIDTH);
            position->y = MulDiv(
                position->y - SPICETOUCH_TOUCH_Y,
                client_rect.bottom,
                SPICETOUCH_TOUCH_HEIGHT);
            return ClientToScreen(window, position) != FALSE;
        }

        // synthetic touches preempt the mouse and keep it disabled until release or timeout
        static void begin_synthetic_contact(HWND window, POINT position) {
            // dedicated TDJ injects into the physical subscreen, so map Windows'
            // returned coordinates back into the game's touch coordinate space
            const auto transform_returned_coordinates = is_tdj_dedicated_subscreen(window);
            if (!transform_synthetic_position(window, &position) ||
                !release_active_contact() ||
                !begin_contact(
                    ContactOwner::Synthetic,
                    window,
                    position,
                    transform_returned_coordinates)) {
                return;
            }

            const auto timer_id = reinterpret_cast<UINT_PTR>(&synthetic_contact_timer_token);
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
                if (w_param) {
                    begin_synthetic_contact(window, position);
                } else {
                    end_synthetic_contact(window);
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
    }

    // inject synthetic touches; only one contact is supported at a time for simplicity
    bool inject_synthetic_touch(POINT position, bool down) {
        internal::initialize_touch_injection();

        const auto window = internal::get_injection_window();
        if (!internal::touch_injection_available() || window == nullptr ||
            internal::synthetic_touch_message == 0) {
            return false;
        }

        return PostMessageW(
            window,
            internal::synthetic_touch_message,
            static_cast<WPARAM>(down),
            MAKELPARAM(position.x, position.y)) != FALSE;
    }
}