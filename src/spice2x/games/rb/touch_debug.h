#pragma once

namespace games::rb {
    extern bool TOUCH_DEBUG_OVERLAY;

    bool touch_debug_overlay_enabled();
    void touch_draw_debug_overlay();

    void touch_debug_attach();
    void touch_debug_detach();
    void touch_debug_publish(const unsigned char *data, bool is_landscape);
}
