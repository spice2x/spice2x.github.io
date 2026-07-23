#pragma once

#include <cstdint>
#include <windows.h>

#include "touch/native/nativetouchhook.h"

namespace games::nost::touch_mode {

    // touch mode forwards contacts to the game; piano mode converts them into piano keys
    enum class Mode {
        Touch,
        Piano,
    };

    void enable();
    void disable();
    bool enabled();

    Mode current_mode();

    void publish_button_bounds(HWND window, const RECT &client_bounds);

    uint32_t piano_key_state();

    bool filter_native_touch(const nativetouch::NativeTouchEvent &event);
}
