#include "configurator_wnd.h"

#include <algorithm>
#include <cstring>

#include <windows.h>
#include <windowsx.h>

#include "build/defs.h"
#include "external/imgui/imgui.h"
#include "launcher/shutdown.h"
#include "overlay/overlay.h"
#include "util/logging.h"
#include "util/precise_timer.h"
#include "cfg/configurator.h"

#include "icon.h"

static const char *CLASS_NAME = "ConfiguratorWindow";
static std::string WINDOW_TITLE;
static int WINDOW_SIZE_X = 800;
static int WINDOW_SIZE_Y = 600;
static HICON WINDOW_ICON = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(MAINICON));

static const UINT_PTR RENDER_TIMER_ID = 1;

// Map a virtual key code to the matching ImGuiKey. Mirrors the table used by
// the in-game ImGui spice backend so navigation keys behave identically when
// the configurator runs standalone and drives ImGui from Win32 messages.
static ImGuiKey vk_to_imgui_key(WPARAM vkey) {
    switch (vkey) {
        case VK_TAB: return ImGuiKey_Tab;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        case VK_PRIOR: return ImGuiKey_PageUp;
        case VK_NEXT: return ImGuiKey_PageDown;
        case VK_HOME: return ImGuiKey_Home;
        case VK_END: return ImGuiKey_End;
        case VK_INSERT: return ImGuiKey_Insert;
        case VK_DELETE: return ImGuiKey_Delete;
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_LSHIFT: return ImGuiKey_LeftShift;
        case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_SHIFT: return ImGuiKey_LeftShift;
        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
        case VK_RCONTROL: return ImGuiKey_RightCtrl;
        case VK_CONTROL: return ImGuiKey_LeftCtrl;
        case 'A': return ImGuiKey_A;
        case 'C': return ImGuiKey_C;
        case 'V': return ImGuiKey_V;
        case 'X': return ImGuiKey_X;
        case 'Y': return ImGuiKey_Y;
        case 'Z': return ImGuiKey_Z;
        default: return ImGuiKey_None;
    }
}

static ImGuiKey vk_to_imgui_mod_key(WPARAM vkey) {
    switch (vkey) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return ImGuiMod_Shift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return ImGuiMod_Ctrl;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return ImGuiMod_Alt;
        default:
            return ImGuiMod_None;
    }
}

static cfg::ConfiguratorWindow *get_state(HWND hWnd) {
    return reinterpret_cast<cfg::ConfiguratorWindow *>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));
}

// Returns the refresh rate (Hz) of the monitor the window currently lives on,
// clamped to a sane range. Falls back to 60 if detection fails or the value
// looks invalid (DEVMODE may report 0 or 1 to mean "use hardware default").
static UINT detect_monitor_refresh_hz(HWND hWnd) {
    UINT hz = 0;
    HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (mon) {
        MONITORINFOEXW mi {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, reinterpret_cast<LPMONITORINFO>(&mi))) {
            DEVMODEW dm {};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                hz = dm.dmDisplayFrequency;
            }
        }
    }
    if (hz <= 1) {
        HDC hdc = GetDC(hWnd);
        if (hdc) {
            int v = GetDeviceCaps(hdc, VREFRESH);
            if (v > 1) {
                hz = static_cast<UINT>(v);
            }
            ReleaseDC(hWnd, hdc);
        }
    }
    if (hz < 30 || hz > 240) {
        hz = 60;
    }
    return hz;
}

// Software-path WM_PAINT: blit the overlay's pixel buffer to the window using
// SetDIBitsToDevice. Avoids creating/destroying a GDI HBITMAP every frame.
static void paint_software(HWND hWnd) {
    if (!overlay::OVERLAY) {
        PAINTSTRUCT ps {};
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return;
    }

    int width = 0;
    int height = 0;
    uint32_t *pixel_data = overlay::OVERLAY->sw_get_pixel_data(&width, &height);

    PAINTSTRUCT paint {};
    HDC hdc = BeginPaint(hWnd, &paint);

    if (pixel_data && width > 0 && height > 0) {
        BITMAPINFO bmi {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        // negative height -> top-down DIB, matching how ImGui packs pixels
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetDIBitsToDevice(
            hdc,
            0, 0,
            width, height,
            0, 0,
            0, height,
            pixel_data,
            &bmi,
            DIB_RGB_COLORS);
    }

    EndPaint(hWnd, &paint);
}

void cfg::ConfiguratorWindow::start_timer() {
    if (!this->timer_running && this->hWnd) {
        SetTimer(this->hWnd, RENDER_TIMER_ID, this->timer_interval_ms, nullptr);
        this->timer_running = true;
    }
}

void cfg::ConfiguratorWindow::stop_timer() {
    if (this->timer_running && this->hWnd) {
        KillTimer(this->hWnd, RENDER_TIMER_ID);
        this->timer_running = false;
    }
}

cfg::ConfiguratorWindow::ConfiguratorWindow() {

    // register the window class
    WNDCLASS wc {};
    wc.lpfnWndProc = ConfiguratorWindow::window_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = NULL;
    wc.hIcon = WINDOW_ICON;
    RegisterClass(&wc);

    // raise SetTimer resolution so high refresh rates (120/144Hz) are actually
    // achievable. Without this, USER timers quantize to ~15.6ms and cap us
    // near ~64 FPS. Uses the shared helper so it respects -notimerhacks
    // (Use Legacy Timers) and the Win11 timer-throttling opt-out. The helper
    // intentionally never calls timeEndPeriod; the kernel releases the request
    // when spicecfg exits.
    timeutils::set_timer_resolution();

    // determine window title
    if (cfg::CONFIGURATOR_TYPE == cfg::ConfigType::Config) {
        WINDOW_TITLE = "spice2x config (" + to_string(VERSION_STRING_CFG) + ")";
        WINDOW_SIZE_X = 800;
        WINDOW_SIZE_Y = 600;
    }
    this->client_width = WINDOW_SIZE_X;
    this->client_height = WINDOW_SIZE_Y;

    // open window
    this->hWnd = CreateWindowEx(
            0,
            CLASS_NAME,
            WINDOW_TITLE.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL,
            NULL,
            GetModuleHandle(NULL),
            (LPVOID) this);

    if (this->hWnd) {
        overlay::USE_WM_CHAR_FOR_IMGUI_CHAR_INPUT = true;
    }
}

cfg::ConfiguratorWindow::~ConfiguratorWindow() {

    this->stop_timer();

    // close window
    DestroyWindow(this->hWnd);

    // unregister class
    UnregisterClass(CLASS_NAME, GetModuleHandle(NULL));

    // release D3D9 resources if any
    if (this->device) {
        this->device->Release();
        this->device = nullptr;
    }
    if (this->d3d) {
        this->d3d->Release();
        this->d3d = nullptr;
    }
}

void cfg::ConfiguratorWindow::run() {

    // show window
    SetWindowPos(this->hWnd, HWND_TOP, 0, 0, WINDOW_SIZE_X, WINDOW_SIZE_Y, 0);
    ShowWindow(this->hWnd, SW_SHOWNORMAL);
    UpdateWindow(this->hWnd);

    // SW_SHOWNORMAL usually activates a top-level window, but not always (e.g.,
    // when the launching process doesn't have foreground rights to transfer).
    // Force foreground+focus explicitly so WM_MOUSEWHEEL is routed to us from
    // the very first frame; the Win32 default routes wheel events to the
    // keyboard-focused window.
    SetForegroundWindow(this->hWnd);
    SetFocus(this->hWnd);

    // match the render timer to the monitor refresh rate so scrolling stays smooth
    // on 60/120/144 Hz panels. Idle CPU is bounded by overlay::sw_pixels_dirty
    // (software path) and overlay::d3d9_frame_dirty (D3D9 path); the timer is also
    // paused entirely when the window is minimized (see WM_SIZE handler).
    const UINT hz = detect_monitor_refresh_hz(this->hWnd);
    this->timer_interval_ms = std::max<UINT>(1, 1000 / hz);
    log_info("configurator", "render timer {} ms ({} Hz)",
        this->timer_interval_ms, hz);
    this->start_timer();

    // window loop
    BOOL ret;
    MSG msg;
    while ((ret = GetMessage(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) {
            break;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

LRESULT CALLBACK cfg::ConfiguratorWindow::window_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CHAR: {

            // input characters if overlay is active
            if (overlay::OVERLAY && overlay::OVERLAY->has_focus()) {
                overlay::OVERLAY->input_char((unsigned int) wParam);
                return true;
            }
            break;
        }
        case WM_CREATE: {

            // set user data of window to class pointer
            auto create_struct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));

            break;
        }
        case WM_SIZE: {

            // pause/resume the render timer based on visibility
            auto *state = get_state(hWnd);
            if (state) {
                if (wParam == SIZE_MINIMIZED) {
                    state->window_minimized = true;
                    state->stop_timer();
                } else {
                    if (state->window_minimized) {
                        state->window_minimized = false;
                        // force a full repaint on the next frame
                        state->has_valid_draw_hash = false;
                        state->start_timer();
                    }
                    const int new_w = LOWORD(lParam);
                    const int new_h = HIWORD(lParam);
                    if (new_w > 0 && new_h > 0
                            && (new_w != state->client_width || new_h != state->client_height)) {
                        state->client_width = new_w;
                        state->client_height = new_h;

                        // reset the D3D9 device with the new back-buffer size so the
                        // hardware-accelerated path matches the window dimensions.
                        if (state->use_d3d9 && state->device) {
                            if (overlay::OVERLAY) {
                                overlay::OVERLAY->reset_invalidate();
                            }
                            state->pp.BackBufferWidth = static_cast<UINT>(new_w);
                            state->pp.BackBufferHeight = static_cast<UINT>(new_h);
                            HRESULT hr = state->device->Reset(&state->pp);
                            if (FAILED(hr)) {
                                log_warning("configurator", "D3D9 device Reset failed, hr={:#x}",
                                    static_cast<unsigned int>(hr));
                            }
                            if (overlay::OVERLAY) {
                                overlay::OVERLAY->reset_recreate();
                            }
                        }

                        // force a full repaint after resize
                        state->has_valid_draw_hash = false;
                    }
                }
            }
            break;
        }
        case WM_DISPLAYCHANGE: {

            // monitor refresh rate may have changed (or the window moved to a
            // different monitor); re-detect and re-arm the render timer.
            auto *state = get_state(hWnd);
            if (state && state->timer_running) {
                const UINT hz = detect_monitor_refresh_hz(hWnd);
                const UINT new_interval = std::max<UINT>(1, 1000 / hz);
                if (new_interval != state->timer_interval_ms) {
                    state->stop_timer();
                    state->timer_interval_ms = new_interval;
                    state->start_timer();
                    log_info("configurator",
                        "WM_DISPLAYCHANGE: render timer {} ms ({} Hz)",
                        new_interval, hz);
                }
            }
            break;
        }
        case WM_CLOSE:
        case WM_DESTROY: {

            // exit process
            launcher::shutdown();
            break;
        }
        case WM_TIMER: {
            if (wParam != RENDER_TIMER_ID) {
                break;
            }

            auto *state = get_state(hWnd);

            // skip rendering when the window is minimized or hidden - the timer is
            // already paused on minimize, but defensively skip here as well.
            if (state && (state->window_minimized || !IsWindowVisible(hWnd))) {
                break;
            }

            const bool use_d3d9 = state && state->use_d3d9 && state->device;

            // build the imgui frame (input is already buffered via WM_* messages)
            if (overlay::OVERLAY) {
                overlay::OVERLAY->update();
                overlay::OVERLAY->set_active(true);
                overlay::OVERLAY->new_frame();
            }

            if (use_d3d9) {
                const HRESULT cl = state->device->TestCooperativeLevel();
                if (cl == D3DERR_DEVICELOST) {
                    break;
                }
                if (cl == D3DERR_DEVICENOTRESET) {
                    if (overlay::OVERLAY) {
                        overlay::OVERLAY->reset_invalidate();
                    }
                    const HRESULT reset_hr = state->device->Reset(&state->pp);
                    if (SUCCEEDED(reset_hr) && overlay::OVERLAY) {
                        overlay::OVERLAY->reset_recreate();
                    }
                    break;
                }
                if (FAILED(cl)) {
                    break;
                }

                if (overlay::OVERLAY) {
                    overlay::OVERLAY->render();
                }

                // skip Clear/Present when ImGui draw data is unchanged; the last
                // presented frame stays visible (same fast path as sw_pixels_dirty).
                const bool dirty = overlay::OVERLAY && overlay::OVERLAY->d3d9_frame_dirty;
                const bool force_present = state && !state->has_valid_draw_hash;
                if (dirty || force_present) {
                    if (state) {
                        state->has_valid_draw_hash = true;
                    }
                    state->device->Clear(0, nullptr, D3DCLEAR_TARGET,
                        D3DCOLOR_RGBA(20, 18, 18, 255), 1.0f, 0);
                    if (SUCCEEDED(state->device->BeginScene())) {
                        if (overlay::OVERLAY) {
                            overlay::OVERLAY->d3d9_render_draw(force_present);
                        }
                        state->device->EndScene();
                    }
                    const HRESULT hr = state->device->Present(nullptr, nullptr, nullptr, nullptr);
                    if (hr == D3DERR_DEVICELOST) {
                        break;
                    }
                }
            } else {
                if (overlay::OVERLAY) {
                    overlay::OVERLAY->render();
                }
                // software path: the overlay's renderer already skipped the costly
                // paint_imgui call when the draw data was unchanged. Only invalidate
                // the window when those pixels actually changed, or on the first
                // frame / after a resize when we lost the previously cached state.
                const bool dirty = overlay::OVERLAY && overlay::OVERLAY->sw_pixels_dirty;
                const bool force_repaint = state && !state->has_valid_draw_hash;
                if (dirty || force_repaint) {
                    if (state) {
                        state->has_valid_draw_hash = true;
                    }
                    // pass FALSE for bErase - WM_ERASEBKGND already short-circuits, so
                    // skipping the erase region avoids one extra Win32 round trip.
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }

            break;
        }
        case WM_ERASEBKGND: {
            return 1;
        }
        case WM_PAINT: {
            paint_software(hWnd);
            break;
        }
        case WM_ACTIVATE: {
            const WORD activation = LOWORD(wParam);
            if (activation == WA_INACTIVE) {
                // dropping any held mouse buttons so we don't get stuck in a
                // "button still pressed" state when we come back.
                auto &io = ImGui::GetIO();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
                io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
            } else {
                // WA_ACTIVE or WA_CLICKACTIVE: make sure the keyboard focus is
                // actually on this window so WM_MOUSEWHEEL gets routed to us
                // again. Without this, scrolling silently stops working after
                // the user alt-tabs back to spicecfg.
                SetFocus(hWnd);
            }
            break;
        }
        case WM_KILLFOCUS: {
            // mirror the WA_INACTIVE mouse-button drop for the keyboard -
            // without this, a key held during alt-tab stays "down" in ImGui
            // state until the user presses and releases it again.
            ImGui::GetIO().ClearInputKeys();
            break;
        }
        // input messages routed straight into ImGui IO. This replaces the previous
        // per-frame 256-VK rawinput scan in ImGui_ImplSpice_NewFrame for the
        // standalone configurator (see CONFIGURATOR_STANDALONE branches there).
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if (wParam == VK_F4) {
                return DefWindowProc(hWnd, uMsg, wParam, lParam);
            }
            const bool down = (uMsg == WM_KEYDOWN) || (uMsg == WM_SYSKEYDOWN);
            auto &io = ImGui::GetIO();
            const ImGuiKey key = vk_to_imgui_key(wParam);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, down);
            }
            const ImGuiKey mod = vk_to_imgui_mod_key(wParam);
            if (mod != ImGuiMod_None) {
                io.AddKeyEvent(mod, down);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            auto &io = ImGui::GetIO();
            io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(lParam)),
                                static_cast<float>(GET_Y_LPARAM(lParam)));
            break;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: {
            int button = 0;
            bool down = false;
            switch (uMsg) {
                case WM_LBUTTONDOWN: button = ImGuiMouseButton_Left; down = true; break;
                case WM_LBUTTONUP: button = ImGuiMouseButton_Left; down = false; break;
                case WM_RBUTTONDOWN: button = ImGuiMouseButton_Right; down = true; break;
                case WM_RBUTTONUP: button = ImGuiMouseButton_Right; down = false; break;
                case WM_MBUTTONDOWN: button = ImGuiMouseButton_Middle; down = true; break;
                case WM_MBUTTONUP: button = ImGuiMouseButton_Middle; down = false; break;
            }
            auto &io = ImGui::GetIO();
            io.AddMouseButtonEvent(button, down);
            if (down) {
                SetCapture(hWnd);
            } else {
                ReleaseCapture();
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam))
                / static_cast<float>(WHEEL_DELTA);
            ImGui::GetIO().AddMouseWheelEvent(0.0f, delta);
            break;
        }
#if !SPICE_XP
        case WM_MOUSEHWHEEL: {
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam))
                / static_cast<float>(WHEEL_DELTA);
            ImGui::GetIO().AddMouseWheelEvent(delta, 0.0f);
            break;
        }
#endif
        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}
