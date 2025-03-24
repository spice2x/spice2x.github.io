#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include <windows.h>
#include <d3d9.h>

#include "external/toojpeg/toojpeg.h"

// order must match spice2x_AutoOrientation UI enum order
enum graphics_orientation {
    ORIENTATION_CW = 0,
    ORIENTATION_CCW = 1,
    ORIENTATION_NORMAL = 2,
};

enum graphics_dx9on12_state {
    DX9ON12_AUTO,
    DX9ON12_FORCE_OFF,
    DX9ON12_FORCE_ON,
};

// flag settings
extern bool GRAPHICS_CAPTURE_CURSOR;
extern bool GRAPHICS_LOG_HRESULT;
extern bool GRAPHICS_SDVX_FORCE_720;
extern bool GRAPHICS_SHOW_CURSOR;
extern bool GRAPHICS_WINDOWED;
extern graphics_orientation GRAPHICS_ADJUST_ORIENTATION;
extern std::vector<HWND> GRAPHICS_WINDOWS;
extern UINT GRAPHICS_FORCE_REFRESH;
extern std::optional<int> GRAPHICS_FORCE_VSYNC_BUFFER;
extern bool GRAPHICS_FORCE_SINGLE_ADAPTER;
extern bool GRAPHICS_PREVENT_SECONDARY_WINDOW;
extern std::optional<std::pair<uint32_t, uint32_t>> GRAPHICS_FS_CUSTOM_RESOLUTION;

extern graphics_dx9on12_state GRAPHICS_9_ON_12_STATE;
extern bool GRAPHICS_9_ON_12_REQUESTED_BY_GAME;

extern std::optional<int> GRAPHICS_WINDOW_STYLE;
extern std::optional<std::pair<uint32_t, uint32_t>> GRAPHICS_WINDOW_SIZE;
extern std::optional<std::string> GRAPHICS_WINDOW_POS;
extern bool GRAPHICS_WINDOW_ALWAYS_ON_TOP;
extern bool GRAPHICS_WINDOW_BACKBUFFER_SCALE;

extern bool GRAPHICS_IIDX_WSUB;
extern std::optional<std::string> GRAPHICS_IIDX_WSUB_SIZE;
extern std::optional<std::string> GRAPHICS_IIDX_WSUB_POS;
extern int GRAPHICS_IIDX_WSUB_WIDTH;
extern int GRAPHICS_IIDX_WSUB_HEIGHT;
extern int GRAPHICS_IIDX_WSUB_X;
extern int GRAPHICS_IIDX_WSUB_Y;
extern HWND TDJ_SUBSCREEN_WINDOW;
extern HWND SDVX_SUBSCREEN_WINDOW;

extern bool SUBSCREEN_FORCE_REDRAW;

// settings
extern std::string GRAPHICS_DEVICEID;
extern std::string GRAPHICS_SCREENSHOT_DIR;

// Direct3D 9 settings
extern std::optional<UINT> D3D9_ADAPTER;
extern DWORD D3D9_BEHAVIOR_DISABLE;
extern bool D3D9_DEVICE_HOOK_DISABLE;

void graphics_init();
void graphics_hook_window(HWND hWnd, D3DPRESENT_PARAMETERS *pPresentationParameters);
void graphics_add_wnd_proc(WNDPROC wndProc);
void graphics_remove_wnd_proc(WNDPROC wndProc);
void graphics_hook_subscreen_window(HWND hWnd);
void graphics_screens_register(int screen);
void graphics_screens_unregister(int screen);
void graphics_screens_get(std::vector<int> &screens);
void graphics_screenshot_trigger();
bool graphics_screenshot_consume();
void graphics_capture_trigger(int screen);
bool graphics_capture_consume(int *screen);
void graphics_capture_enqueue(int screen, uint8_t *data, size_t width, size_t height);
void graphics_capture_skip(int screen);
bool graphics_capture_receive_jpeg(int screen, TooJpeg::WRITE_ONE_BYTE receiver,
        bool rgb = true, int quality = 80, bool downsample = true, int divide = 0,
        uint64_t *timestamp = nullptr,
        int *width = nullptr, int *height = nullptr);
std::string graphics_screenshot_genpath();

// graphics_windowed.cpp
void graphics_windowed_wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void graphics_capture_initial_window(HWND hWnd);
void graphics_update_window_style(HWND hWnd);
void graphics_update_z_order(HWND hWnd);
void graphics_move_resize_window(HWND hWnd);
bool graphics_window_change_crashes_game();
void graphics_load_windowed_subscreen_parameters();
