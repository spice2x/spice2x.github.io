#pragma once

#include <memory>
#include <mutex>
#include <functional>
#include <vector>
#include <windows.h>
#include <d3d9.h>

#include "external/imgui/imgui.h"

namespace overlay {
    class Window;

    enum class OverlayRenderer {
        D3D9,
        SOFTWARE,
    };

    // settings
    extern bool ENABLED;
    extern bool AUTO_SHOW_FPS;
    extern bool AUTO_SHOW_SUBSCREEN;
    extern bool AUTO_SHOW_IOPANEL;
    extern bool AUTO_SHOW_KEYPAD_P1;
    extern bool AUTO_SHOW_KEYPAD_P2;
    extern bool USE_WM_CHAR_FOR_IMGUI_CHAR_INPUT;
    extern bool FPS_SHOULD_FLIP;

    class SpiceOverlay {
    public:

        D3DDEVICE_CREATION_PARAMETERS creation_parameters {};
        D3DADAPTER_IDENTIFIER9 adapter_identifier {};
        bool hotkeys_enable = true;

        // windows
        Window *window_fps = nullptr;
        Window *window_iopanel = nullptr;
        Window *window_config = nullptr;
        Window *window_keypad1 = nullptr;
        Window *window_keypad2 = nullptr;
        Window *window_cards = nullptr;
        Window *window_control = nullptr;
        Window *window_resize = nullptr;
        Window *window_camera = nullptr;
        Window *window_sub = nullptr;
        Window *window_log = nullptr;

        explicit SpiceOverlay(HWND hWnd, IDirect3D9 *d3d, IDirect3DDevice9 *device);
        explicit SpiceOverlay(HWND hWnd);
        ~SpiceOverlay();

        void window_add(Window *wnd);
        void new_frame();
        void render();
        void update();
        void toggle_active(bool overlay_key = false);
        void show_main_menu();
        void set_active(bool active);
        bool get_active();
        bool has_focus();
        bool hotkeys_triggered();

        static bool update_cursor();
        static void reset_invalidate();
        static void reset_recreate();
        void input_char(unsigned int c);

        uint32_t *sw_get_pixel_data(int *width, int *height);

        inline bool uses_window(HWND hWnd) {
            return this->hWnd == hWnd;
        }
        inline bool uses_context(IDirect3D9 *other) {
            return this->d3d == other;
        }
        inline bool uses_device(IDirect3DDevice9 *other) {
            return this->device == other;
        }
        inline IDirect3DDevice9 *get_device() {
            return this->device;
        }

        bool can_transform_touch_input() {
            return (this->subscreen_mouse_handler != nullptr);
        }

        bool transform_touch_point(LONG *x, LONG *y) {
            if (this->get_active() && this->subscreen_mouse_handler) {
                return this->subscreen_mouse_handler(x, y);
            } else {
                return true;
            }
        }

        void set_subscreen_mouse_handler(const std::function<bool(LONG *, LONG *)> &f) {
            this->subscreen_mouse_handler = f;
        }

        // renderer
        OverlayRenderer renderer;
        float total_elapsed = 0.f;

    private:

        HWND hWnd = nullptr;

        // D3D9
        IDirect3D9 *d3d = nullptr;
        IDirect3DDevice9 *device = nullptr;

        // software
        std::vector<uint32_t> pixel_data;
        size_t pixel_data_width = 0;
        size_t pixel_data_height = 0;

        std::vector<std::unique_ptr<Window>> windows;

        Window *window_main_menu = nullptr;

        std::function<bool(LONG *, LONG *)> subscreen_mouse_handler = nullptr;

        bool active = false;
        bool toggle_down = false;
        bool main_menu_down = false;
        bool hotkey_toggle = false;
        bool hotkey_toggle_last = false;

        void init();
    };

    // global
    extern std::mutex OVERLAY_MUTEX;
    extern std::unique_ptr<overlay::SpiceOverlay> OVERLAY;
    extern ImFont* DSEG_FONT;

    // synchronized helpers
    void create_d3d9(HWND hWnd, IDirect3D9 *d3d, IDirect3DDevice9 *device);
    void create_software(HWND hWnd);
    void destroy(HWND hWnd = nullptr);
}
