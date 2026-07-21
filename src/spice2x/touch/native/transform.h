#pragma once

#include <windows.h>

namespace nativetouch::transform {
    enum class Result {
        Unchanged,
        Transformed,
        Rejected,
    };

    bool is_tdj_dedicated_subscreen(HWND window);
    bool game_to_screen(HWND window, POINT *position);
    bool screen_to_game(HWND window, POINT *position);
    Result hardware_to_game(POINT *position);
}
