#pragma once

#include <windows.h>

struct tagTOUCHINPUT;

namespace nativetouch::inject {
    void attach_window(HWND window);
    void register_and_attach_window(HWND window);
    void set_preferred_injection_window(HWND window);
    bool hook_available(HMODULE module);
    bool hook(HMODULE module);
    bool inject_synthetic_touch(POINT position, bool down);
    bool inject_synthetic_touch_from_canvas(POINT position, SIZE canvas, bool down);
    bool transform_touch_input(tagTOUCHINPUT *point);
}
