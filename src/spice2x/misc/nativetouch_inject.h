#pragma once

#include <windows.h>

namespace nativetouch_inject {
    void attach_window(HWND window);
    void register_and_attach_window(HWND window);
    void hook(HMODULE module);
    bool inject_external_touch(POINT position, bool down);
    void transform_touch_input(PTOUCHINPUT point);
}
