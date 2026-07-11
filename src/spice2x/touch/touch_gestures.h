#pragma once

#include <windows.h>

// disable the OS touch UX for a window: visual feedback (contact circles,
// tap/press-and-hold indicators) and gesture behaviors (press-and-hold
// right-click, flicks, etc.)
void disable_touch_gestures(HWND hwnd);
