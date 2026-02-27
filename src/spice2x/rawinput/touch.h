#pragma once

#include "device.h"
#include "rawinput.h"

namespace rawinput::touch {

    // settings
    extern bool DISABLED;
    extern bool INVERTED;

    // global state
    extern DWORD DISPLAY_ORIENTATION;
    extern long DISPLAY_SIZE_X;
    extern long DISPLAY_SIZE_Y;
    extern bool DISPLAY_INITIALIZED;

    bool is_touchscreen(Device *device);
    void enable(Device *device);
    void disable(Device *device);
    void update_input(Device *device);
    void update_timeouts(Device *device);
    void update_timeouts(RawInputManager *manager);
    bool is_enabled(RawInputManager *manager);
    void display_update();
}
