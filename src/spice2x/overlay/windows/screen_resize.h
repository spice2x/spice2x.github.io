#pragma once

#include "overlay/window.h"

namespace overlay::windows {

    class ScreenResize : public Window {
    public:
        ScreenResize(SpiceOverlay *overlay);
        ~ScreenResize() override;

        void build_content() override;
        void update();
        
    private:
        size_t toggle_screen_resize = ~0u;
        bool toggle_screen_resize_state = false;

        size_t toggle_scene[4] = { ~0u, ~0u, ~0u, ~0u };
        uint32_t toggle_scene_state = ~0u;

        void build_fullscreen_config();
        void build_windowed_config();
        void build_footer();
        std::string hwnd_preview(int index, HWND hwnd);
        HWND get_first_window();

        void reset_window();
        void reset_vars_to_default();

        static LRESULT CALLBACK screen_resize_wndproc(
            HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        static void wndproc_wm_sizing(int edge, RECT& rect);
    };
}
