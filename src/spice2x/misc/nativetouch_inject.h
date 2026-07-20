#pragma once

#include <windows.h>

struct tagTOUCHINPUT;

namespace nativetouch_inject {
    void attach_window(HWND window);
    void register_and_attach_window(HWND window);
    void hook(HMODULE module);
    bool inject_external_touch(POINT position, bool down);
    bool transform_touch_input(tagTOUCHINPUT *point);
}
