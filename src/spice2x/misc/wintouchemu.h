#pragma once

#include <windows.h>

namespace wintouchemu {

    // settings
    extern bool FORCE;
    extern bool INJECT_MOUSE_AS_WM_TOUCH;
    extern bool ADD_TOUCH_FLAG_PRIMARY;

    void hook(const char *window_title, HMODULE module = nullptr);
    void hook_title_ends(
        const char *window_title_start,
        const char *window_title_end,
        HMODULE module = nullptr);
    void update();
}
