#pragma once

#include "device.h"
#include "rawinput.h"

namespace rawinput::touch {

    // settings
    extern bool DISABLED;
    extern bool INVERTED;

    bool is_touchscreen(Device *device);
    void enable(Device *device);
    void disable(Device *device);
    void update_input(Device *device);
    void update_timeouts(Device *device);
    void update_timeouts(RawInputManager *manager);
    bool is_enabled(RawInputManager *manager);
    void display_update();
}
