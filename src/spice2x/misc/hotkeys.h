#pragma once

namespace hotkeys {

    // ALT+F4 monitoring spans boot and teardown; configured actions are enabled separately
    void start();
    void enable_raw_input();
    void enable_input();
    void disable_input();
    void disable_raw_input();
    void stop();
    bool consume_screenshot();
}
