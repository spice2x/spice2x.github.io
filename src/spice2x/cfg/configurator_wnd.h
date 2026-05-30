#pragma once

#include <cstdint>

#include <windows.h>
#include <d3d9.h>

namespace cfg {

    class ConfiguratorWindow {
    public:

        HWND hWnd;

        // optional D3D9 device backing the configurator window; nullptr when running
        // the software-rendered path. Owned by ConfiguratorWindow when set.
        IDirect3D9 *d3d = nullptr;
        IDirect3DDevice9 *device = nullptr;
        D3DPRESENT_PARAMETERS pp {};
        bool use_d3d9 = false;

        // throttling / pause state. timer_interval_ms is the default until run()
        // refines it to the actual monitor refresh rate (see detect_monitor_refresh_hz).
        UINT timer_interval_ms = 1000 / 60;
        bool timer_running = false;
        bool window_minimized = false;

        // tracks whether we've issued at least one InvalidateRect since window
        // creation/resize. The overlay's per-frame "pixels changed" flag suppresses
        // idle blits; this flag forces the very first blit on startup or after
        // a resize so the window doesn't show garbage until the user moves the mouse.
        bool has_valid_draw_hash = false;

        // dimensions of the configurator client area (kept in sync with WM_SIZE)
        int client_width = 0;
        int client_height = 0;

        ConfiguratorWindow();
        ~ConfiguratorWindow();

        void run();

        // start/stop the render timer based on visibility state
        void start_timer();
        void stop_timer();

        static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    };
}
