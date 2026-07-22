#pragma once

#include <windows.h>

namespace wintouchemu {

    // settings
    extern bool FORCE;
    extern bool INJECT_MOUSE_AS_WM_TOUCH;
    extern bool LOG_FPS;
    extern bool ADD_TOUCH_FLAG_PRIMARY;

    void hook(const char *window_title, HMODULE module = nullptr);
    void update();
}
