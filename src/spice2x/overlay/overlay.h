#pragma once

#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include <vector>
#include <windows.h>
#include <d3d9.h>

#include "external/imgui/imgui.h"

#ifdef SPICE_D3D11
// forward decls for D3D11 (avoid pulling d3d11.h into every TU)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;
#endif

namespace overlay {
    class Window;

    enum class OverlayRenderer {
        D3D9,
#ifdef SPICE_D3D11
        D3D11,
#endif
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
    extern bool SHOW_DEBUG_LOG_WINDOW;
    extern std::optional<uint32_t> UI_SCALE_PERCENT;

    template <typename T>
    float apply_scaling(T input) {
        if (!UI_SCALE_PERCENT.has_value()) {
            return input;
        }
        return static_cast<float>(input) * UI_SCALE_PERCENT.value() / 100.f;
    }

    ImVec2 apply_scaling_to_vector(const ImVec2& input);
    ImVec2 apply_scaling_to_vector(float x, float y);

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
#ifdef SPICE_D3D11
        explicit SpiceOverlay(HWND hWnd, ID3D11Device *d3d11_device,
                              ID3D11DeviceContext *d3d11_context, IDXGISwapChain *d3d11_swapchain);
#endif
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
#ifdef SPICE_D3D11
        inline bool uses_device(ID3D11Device *other) {
            return this->d3d11_device == other;
        }
        inline bool uses_swapchain(IDXGISwapChain *other) {
            return this->d3d11_swapchain == other;
        }
#endif
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

#ifdef SPICE_D3D11
        // D3D11
        ID3D11Device *d3d11_device = nullptr;
        ID3D11DeviceContext *d3d11_context = nullptr;
        IDXGISwapChain *d3d11_swapchain = nullptr;
        ID3D11RenderTargetView *d3d11_rtv = nullptr;
#endif

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

        // true between new_frame()'s ImGui::NewFrame() and render()'s ImGui::Render(),
        // so render() never runs without a matching NewFrame.
        bool has_pending_frame = false;

        void init();
        void add_font(const char* font, ImFontConfig* config, const ImWchar* glyphs);
    };

    // global
    extern std::mutex OVERLAY_MUTEX;
    extern std::unique_ptr<overlay::SpiceOverlay> OVERLAY;
    extern ImFont* DSEG_FONT;

    // synchronized helpers
    void create_d3d9(HWND hWnd, IDirect3D9 *d3d, IDirect3DDevice9 *device);
#ifdef SPICE_D3D11
    void create_d3d11(HWND hWnd, ID3D11Device *device, ID3D11DeviceContext *context,
                      IDXGISwapChain *swapchain);
#endif
    void create_software(HWND hWnd);
    void destroy(HWND hWnd = nullptr);
}
