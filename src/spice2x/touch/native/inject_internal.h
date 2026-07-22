#pragma once

#include <windows.h>

namespace nativetouch::inject {

    enum class ContactOwner {
        None,
        Mouse,
        Synthetic,
    };

    void initialize_touch_injection();
    void initialize_synthetic_touch();
    bool touch_injection_available();
    void refresh_contact_lifetime();

    bool contact_is_active();
    bool contact_is_owned_by(ContactOwner owner, HWND window);
    bool begin_contact(
        ContactOwner owner,
        HWND window,
        POINT position,
        bool transform_returned_coordinates);
    bool update_contact(ContactOwner owner, HWND window, POINT position);
    void set_contact_timer(ContactOwner owner, HWND window, UINT_PTR timer_id);
    bool release_active_contact();

    HWND get_injection_window();
    bool handle_mouse_message(HWND window, UINT message, WPARAM w_param);
    bool handle_synthetic_message(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
}
