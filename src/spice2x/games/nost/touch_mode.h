#pragma once

#include <cstdint>
#include <windows.h>

namespace games::nost::touch_mode {

    // touch mode forwards contacts to the game; piano mode converts them into piano keys
    enum class Mode {
        Touch,
        Piano,
    };

    // install or remove the Nostalgia-specific native touch interception
    void enable();
    void disable();
    bool enabled();

    Mode current_mode();
    void toggle();

    // publish the rendered overlay button rectangle in game-client pixels
    void publish_button_bounds(HWND window, const RECT &client_bounds);

    // consume one completed hardware-touch activation of the mode button
    bool consume_mode_button_activation();

    // return the active 28-key piano bitfield for the PANB input update
    uint32_t piano_key_state();

    // mirror hardware contacts and report whether the game should receive this event
    bool filter_native_touch(const TOUCHINPUT &point, bool synthetic);
}