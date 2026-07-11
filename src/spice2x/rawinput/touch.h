#pragma once

#include "device.h"
#include "rawinput.h"

namespace rawinput::touch {

    // settings
    extern bool DISABLED;
    extern bool INVERTED;

    // aspect-ratio compensation: when the current display mode has a different aspect
    // ratio than the panel's native resolution, the image is letterboxed/pillarboxed
    // on the physical panel while the digitizer still spans the whole panel. this
    // remaps the touch into the displayed image region.
    enum class AspectMode {
        Auto, // decided per game via ASPECT_COMPENSATION_GAME
        On,   // always compensate
        Off,  // never compensate
    };

    // global override, driven by the -rawtouchaspect launcher option
    extern AspectMode ASPECT_COMPENSATION_MODE;

    // per-game request, honored only when ASPECT_COMPENSATION_MODE is Auto
    extern bool ASPECT_COMPENSATION_GAME;

    // effective decision combining the global mode and the per-game request
    bool aspect_compensation_enabled();

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
