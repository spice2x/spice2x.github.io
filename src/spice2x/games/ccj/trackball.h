#pragma once
#include <cstdint>

namespace games::ccj {
    extern bool MOUSE_TRACKBALL;
    extern bool MOUSE_TRACKBALL_USE_TOGGLE;
    extern uint8_t TRACKBALL_SENSITIVITY;

    void trackball_hook_init();
    void trackball_thread_start();
    void trackball_thread_stop();
}
