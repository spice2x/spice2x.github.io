#pragma once

#include <windows.h>

namespace wintouchemu {

    // settings
    extern bool FORCE;
    extern bool ADD_TOUCH_FLAG_PRIMARY;

    void hook(const char *window_title, HMODULE module = nullptr);
    void update();
}
