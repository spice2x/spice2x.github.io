#pragma once

#include <bitset>
#include <cstdint>
#include <windows.h>

namespace games::jb {

    // touch detection algorithm
    enum JubeatTouchAlgorithm {
        Legacy,
        Improved,
        Plus,
        AcAccurate
    };

    // debug touch overlay mode
    enum JubeatTouchDebugMode {
        JbTouchDebugAuto,   // boundary boxes when a touch screen is detected, else nothing
        JbTouchDebugNone,   // draw nothing
        JbTouchDebugBox,    // boundary boxes
        JbTouchDebugAll,    // boundary boxes and touch circles
    };

    // selected algorithm and debug mode (set from the launcher options)
    extern JubeatTouchAlgorithm TOUCH_ALGORITHM;
    extern JubeatTouchDebugMode TOUCH_DEBUG_OVERLAY;

    // atomically published panel state, read by the I/O layer
    std::bitset<16> touch_state();

    // read the current touch points and refresh the panel state
    void touch_update();

    // enable/disable touch handling on game attach/detach
    void touch_attach();
    void touch_detach();

    // whether the debug overlay should currently draw anything
    bool touch_debug_overlay_enabled();

    // draw the debug touch overlay (boundary boxes and/or touch circles per the
    // selected mode) onto the spice touch overlay window's device context
    void touch_draw_debug_overlay(HDC hdc);
}
