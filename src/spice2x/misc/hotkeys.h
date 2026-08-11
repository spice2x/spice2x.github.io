#pragma once

#include <cstddef>
#include <cstdint>

namespace hotkeys {

    enum class Action : uint8_t {
        InsertCardP1,
        InsertCardP2,
        Count,
    };

    // fixed ALT+F4 monitoring spans boot and teardown; configured input is enabled separately
    void start();
    void enable_raw_input();
    void enable_input(bool hotkey_gate_enabled);
    void disable_input();
    void disable_raw_input();
    void stop();
    bool consume(Action action);
    bool consume_overlay_button(size_t index);
    void enable_hotkey_gate();
    void disable_hotkey_gate();
    bool pressed(Action action);
}
