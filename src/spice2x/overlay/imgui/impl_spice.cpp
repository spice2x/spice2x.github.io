#include "impl_spice.h"

#include <windows.h>

#include "cfg/configurator.h"
#include "games/io.h"
#include "launcher/launcher.h"
#include "launcher/superexit.h"
#include "misc/eamuse.h"
#include "overlay/overlay.h"
#include "rawinput/rawinput.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/utils.h"
#include "hooks/graphics/graphics.h"

#if !defined(IMGUI_ENABLE_WIN32_DEFAULT_IME_FUNCTIONS) || \
    !defined(IMGUI_DISABLE_DEFAULT_ALLOCATORS) || \
    !defined(IMGUI_USE_BGRA_PACKED_COLOR) || \
    !defined(IMGUI_HAS_VIEWPORT) || \
    !defined(IMGUI_HAS_DOCK) || \
    !defined(IMGUI_DISABLE_DEMO_WINDOWS) || \
    defined(IMGUI_DISABLE_DEBUG_TOOLS)
#error "fix imconfig.h after updating imgui version"
#endif

// state
static HWND g_hWnd = nullptr;
static INT64 g_Time = 0;
static INT64 g_TicksPerSecond = 0;
static ImGuiMouseCursor g_LastMouseCursor = ImGuiMouseCursor_COUNT;

bool ImGui_ImplSpice_Init(HWND hWnd) {
    log_misc("imgui_impl_spice", "init");

    // check if already initialized
    if (g_hWnd != nullptr) {
        if (g_hWnd == hWnd) {
            return true;
        } else {
            ImGui_ImplSpice_Shutdown();
        }
    }

    // init performance stuff
    if (!::QueryPerformanceFrequency((LARGE_INTEGER *)&g_TicksPerSecond))
        return false;
    if (!::QueryPerformanceCounter((LARGE_INTEGER *)&g_Time))
        return false;

    // setup back-end capabilities flags
    g_hWnd = hWnd;
    ImGuiIO &io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
    io.BackendPlatformName = "imgui_impl_spice";
    io.ConfigErrorRecoveryEnableTooltip = true;

    // keyboard mapping
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
    io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
    io.KeyMap[ImGuiKey_Home] = VK_HOME;
    io.KeyMap[ImGuiKey_End] = VK_END;
    io.KeyMap[ImGuiKey_Insert] = VK_INSERT;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Space] = VK_SPACE;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
    io.KeyMap[ImGuiKey_KeypadEnter] = VK_RETURN;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    // get display size
    ImGui_ImplSpice_UpdateDisplaySize();

    // return success
    return true;
}

void ImGui_ImplSpice_Shutdown() {
    log_misc("imgui_impl_spice", "shutdown");

    // reset window handle
    g_hWnd = nullptr;
}

void ImGui_ImplSpice_UpdateDisplaySize() {

    // get display size
    RECT rect;
    ::GetClientRect(g_hWnd, &rect);
    ImGui::GetIO().DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
}

bool ImGui_ImplSpice_UpdateMouseCursor() {

    // check if cursor should be changed
    auto &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) {
        return false;
    }

    // update cursor
    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {

        // hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        ::SetCursor(nullptr);

    } else {

        // show OS mouse cursor
        LPTSTR win32_cursor = IDC_ARROW;
        switch (imgui_cursor) {
            case ImGuiMouseCursor_Arrow:
                win32_cursor = IDC_ARROW;
                break;
            case ImGuiMouseCursor_TextInput:
                win32_cursor = IDC_IBEAM;
                break;
            case ImGuiMouseCursor_ResizeAll:
                win32_cursor = IDC_SIZEALL;
                break;
            case ImGuiMouseCursor_ResizeEW:
                win32_cursor = IDC_SIZEWE;
                break;
            case ImGuiMouseCursor_ResizeNS:
                win32_cursor = IDC_SIZENS;
                break;
            case ImGuiMouseCursor_ResizeNESW:
                win32_cursor = IDC_SIZENESW;
                break;
            case ImGuiMouseCursor_ResizeNWSE:
                win32_cursor = IDC_SIZENWSE;
                break;
            case ImGuiMouseCursor_Hand:
                win32_cursor = IDC_HAND;
                break;
            default:
                break;
        }
        ::SetCursor(::LoadCursor(nullptr, win32_cursor));
    }
    return true;
}

static void ImGui_ImplSpice_UpdateMousePos() {

    // get current window size
    RECT rect;
    if (GetClientRect(g_hWnd, &rect)) {
        ImVec2 window_size(
                (float)(rect.right - rect.left),
                (float)(rect.bottom - rect.top));

        // set OS mouse position if requested
        auto &io = ImGui::GetIO();
        if (io.WantSetMousePos) {
            POINT pos {
                .x = static_cast<long>(io.MousePos.x),
                .y = static_cast<long>(io.MousePos.y),
            };
            ::ClientToScreen(g_hWnd, &pos);
            ::SetCursorPos(
                    static_cast<int>(pos.x / io.DisplaySize.x * window_size.x),
                    static_cast<int>(pos.y / io.DisplaySize.y * window_size.y));
        }

        const auto active_window = ::GetForegroundWindow();

        // if the main focus is a windowed subscreen, put the imgui cursor in a place that won't
        // trigger any overlay, don't process anything else
        const auto is_windowed_subscreen = 
            (GRAPHICS_IIDX_WSUB && active_window == TDJ_SUBSCREEN_WINDOW) ||
            (active_window == SDVX_SUBSCREEN_WINDOW);
        if (is_windowed_subscreen) {
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
            return;
        }

        // set mouse position
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        POINT pos;
        if (active_window) {
            if (active_window == g_hWnd
            || ::IsChild(active_window, g_hWnd)
            || ::IsChild(g_hWnd, active_window)
            || active_window == SPICETOUCH_TOUCH_HWND) {
                if (::GetCursorPos(&pos) && ::ScreenToClient(g_hWnd, &pos)) {
                    io.MousePos = ImVec2(
                            (float) pos.x * io.DisplaySize.x / window_size.x,
                            (float) pos.y * io.DisplaySize.y / window_size.y);
                }
            }
        }

        // fallback to touch hwnd
        if (io.MousePos.x == -FLT_MAX || io.MousePos.y == -FLT_MAX) {
            if (SPICETOUCH_TOUCH_HWND) {
                if (::GetCursorPos(&pos) && ::ScreenToClient(SPICETOUCH_TOUCH_HWND, &pos)) {
                    io.MousePos = ImVec2(
                            (float) pos.x * io.DisplaySize.x / window_size.x,
                            (float) pos.y * io.DisplaySize.y / window_size.y);
                }
            }
        }

        // alternatively check touch
        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points, true);
        static size_t delay_touch = 0;
        static size_t delay_touch_target = 2;
        static DWORD last_touch_id = ~0u;
        if (!touch_points.empty() && !is_windowed_subscreen) {

            // use the first touch point
            auto &tp = touch_points[0];
            io.MousePos.x = tp.x * io.DisplaySize.x / window_size.x;
            io.MousePos.y = tp.y * io.DisplaySize.y / window_size.y;

            // update cursor position
            if (!tp.mouse) {
                pos.x = static_cast<long>(io.MousePos.x);
                pos.y = static_cast<long>(io.MousePos.y);
                ::ClientToScreen(g_hWnd, &pos);
                ::SetCursorPos(
                        static_cast<long>(pos.x / io.DisplaySize.x * window_size.x),
                        static_cast<long>(pos.y / io.DisplaySize.y * window_size.y));
            }

            // delay press
            io.MouseDown[0] = delay_touch++ >= delay_touch_target && last_touch_id == tp.id;
            if (last_touch_id == ~0u) {
                last_touch_id = tp.id;
            }

        } else {

            // reset
            delay_touch = 0;
            last_touch_id = ~0;
        }
    }
}

void ImGui_ImplSpice_NewFrame() {

    // check if font is built
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.Fonts->IsBuilt());

    // setup time step
    INT64 current_time;
    ::QueryPerformanceCounter((LARGE_INTEGER *)&current_time);
    io.DeltaTime = (float) (current_time - g_Time) / g_TicksPerSecond;
    g_Time = current_time;

    // remember old state
    BYTE KeysDownOld[sizeof(io.KeysDown)];
    for (size_t i = 0; i < sizeof(io.KeysDown); i++) {
        KeysDownOld[i] = io.KeysDown[i] ? ~0 : 0;
    }
    KeysDownOld[VK_SHIFT] |= KeysDownOld[VK_LSHIFT];
    KeysDownOld[VK_SHIFT] |= KeysDownOld[VK_RSHIFT];

    // reset keys state
    io.MouseWheel = 0;
    io.KeyCtrl = false;
    io.KeyShift = false;
    io.KeyAlt = false;
    io.KeySuper = false;
    memset(io.KeysDown, false, sizeof(io.KeysDown));
    memset(io.MouseDown, false, sizeof(io.MouseDown));

    // early quit if window not in focus
    if (!superexit::has_focus()) {
        return;
    }

    // read keyboard modifiers inputs
    io.KeyCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    io.KeyShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    io.KeyAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
    io.KeySuper = (::GetKeyState(VK_LWIN) & 0x8000) != 0;
    io.KeySuper |= (::GetKeyState(VK_RWIN) & 0x8000) != 0;

    // apply windows mouse buttons
    io.MouseDown[0] |= (get_async_primary_mouse()) != 0;
    io.MouseDown[1] |= (get_async_secondary_mouse()) != 0;
    io.MouseDown[2] |= (GetAsyncKeyState(VK_MBUTTON)) != 0;

    // read new keys state
    static long mouse_wheel_last = 0;
    long mouse_wheel = 0;
    if (RI_MGR != nullptr) {
        auto devices = RI_MGR->devices_get();
        for (auto &device : devices) {
            switch (device.type) {
                case rawinput::MOUSE: {
                    auto &mouse = device.mouseInfo;

                    // mouse button triggers
                    if (GetSystemMetrics(SM_SWAPBUTTON)) {
                        if (mouse->key_states[rawinput::MOUSEBTN_RIGHT]) {
                            io.MouseDown[0] = true;
                        }
                        if (mouse->key_states[rawinput::MOUSEBTN_LEFT]) {
                            io.MouseDown[1] = true;
                        }
                    } else {
                        if (mouse->key_states[rawinput::MOUSEBTN_LEFT]) {
                            io.MouseDown[0] = true;
                        }
                        if (mouse->key_states[rawinput::MOUSEBTN_RIGHT]) {
                            io.MouseDown[1] = true;
                        }
                    }
                    if (mouse->key_states[rawinput::MOUSEBTN_MIDDLE]) {
                        io.MouseDown[2] = true;
                    }

                    // final mouse wheel value should be all devices combined
                    mouse_wheel += mouse->pos_wheel;

                    break;
                }
                case rawinput::KEYBOARD: {

                    // iterate all virtual key codes
                    for (size_t vKey = 0; vKey < 256; vKey++) {

                        // get state (combined from all pages)
                        auto &key_states = device.keyboardInfo->key_states;
                        bool state = false;
                        for (size_t page_index = 0; page_index < 1024; page_index += 256) {
                            state |= key_states[page_index + vKey];
                        }

                        // trigger
                        io.KeysDown[vKey] |= state;

                        // generate character input, but only if WM_CHAR didn't take over the
                        // functionality
                        if (!overlay::USE_WM_CHAR_FOR_IMGUI_CHAR_INPUT && !KeysDownOld[vKey] && state) {
                            UCHAR buf[2];
                            auto ret = ToAscii(
                                    static_cast<UINT>(vKey),
                                    0,
                                    static_cast<const BYTE *>(KeysDownOld),
                                    reinterpret_cast<LPWORD>(buf),
                                    0);
                            if (ret > 0) {
                                for (int i = 0; i < ret; i++) {
                                    overlay::OVERLAY->input_char(buf[i]);
                                }
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    // set mouse wheel
    auto mouse_diff = mouse_wheel - mouse_wheel_last;
    mouse_wheel_last = mouse_wheel;
    io.MouseWheel = mouse_diff;

    // update OS mouse position
    ImGui_ImplSpice_UpdateMousePos();

    if (cfg::CONFIGURATOR_STANDALONE) {
        // if cursor is inside the client area, always set the OS cursor to what ImGui wants
        // this is to deal with cases where mouse cursor changes outside the client rect and comes
        // back into the window
        // i'm sure there might be better ways to deal with this but this works so whatever, right?
        RECT client_rect;
        if (GetClientRect(g_hWnd, &client_rect)) {
            POINT cursor;
            if (GetCursorPos(&cursor) && ScreenToClient(g_hWnd, &cursor)) {
                if (client_rect.left < cursor.x && cursor.x < client_rect.right &&
                    client_rect.top < cursor.y && cursor.y < client_rect.bottom) {
                    ImGui_ImplSpice_UpdateMouseCursor();
                }
            }
        }
    } else {
        // update OS mouse cursor with the cursor requested by imgui
        ImGuiMouseCursor mouse_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
        if (g_LastMouseCursor != mouse_cursor) {
            g_LastMouseCursor = mouse_cursor;
            ImGui_ImplSpice_UpdateMouseCursor();
        }
    }
}
