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

static void ImGui_ImplSpice_UpdateMousePos(bool *mouseleft_down) {
    ImVec2 mousepos(-FLT_MAX, -FLT_MAX);

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

        // set mouse position
        POINT pos;
        if (HWND active_window = ::GetForegroundWindow()) {
            if (active_window == g_hWnd
            || ::IsChild(active_window, g_hWnd)
            || ::IsChild(g_hWnd, active_window)
            || active_window == SPICETOUCH_TOUCH_HWND) {
                if (::GetCursorPos(&pos) && ::ScreenToClient(g_hWnd, &pos)) {
                    mousepos = ImVec2(
                            (float) pos.x * io.DisplaySize.x / window_size.x,
                            (float) pos.y * io.DisplaySize.y / window_size.y);
                }
            }
        }

        // fallback to touch hwnd
        if (mousepos.x == -FLT_MAX || mousepos.y == -FLT_MAX) {
            if (SPICETOUCH_TOUCH_HWND) {
                if (::GetCursorPos(&pos) && ::ScreenToClient(SPICETOUCH_TOUCH_HWND, &pos)) {
                    mousepos = ImVec2(
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
        if (!touch_points.empty()) {

            // use the first touch point
            auto &tp = touch_points[0];
            mousepos.x = tp.x * io.DisplaySize.x / window_size.x;
            mousepos.y = tp.y * io.DisplaySize.y / window_size.y;

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
            *mouseleft_down = delay_touch++ >= delay_touch_target && last_touch_id == tp.id;
            if (last_touch_id == ~0u) {
                last_touch_id = tp.id;
            }

        } else {

            // reset
            delay_touch = 0;
            last_touch_id = ~0;
        }

        // finally, inject events to ImGui
        if (mousepos.x != -FLT_MAX && mousepos.y != -FLT_MAX) {
            io.AddMousePosEvent(mousepos.x, mousepos.y);
        }
    }
}

// previous keyboard state (using BYTE[] for use with ToAscii routine)
BYTE KeysDownOld[255] = {};

void ImGui_ImplSpice_NewFrame() {

    // check if font is built
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.Fonts->IsBuilt());

    // setup time step
    INT64 current_time;
    ::QueryPerformanceCounter((LARGE_INTEGER *)&current_time);
    io.DeltaTime = (float) (current_time - g_Time) / g_TicksPerSecond;
    g_Time = current_time;

    // new keyboard state combining all devices
    BYTE KeysDownNew[255] = {};

    // early quit if window not in focus
    if (!superexit::has_focus()) {
        return;
    }

    // mouse states, combining all devices
    bool mouse_state[3] = {};

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
                            mouse_state[ImGuiMouseButton_Left] = true;
                        }
                        if (mouse->key_states[rawinput::MOUSEBTN_LEFT]) {
                            mouse_state[ImGuiMouseButton_Right] = true;
                        }
                    } else {
                        if (mouse->key_states[rawinput::MOUSEBTN_LEFT]) {
                            mouse_state[ImGuiMouseButton_Left] = true;
                        }
                        if (mouse->key_states[rawinput::MOUSEBTN_RIGHT]) {
                            mouse_state[ImGuiMouseButton_Right] = true;
                        }
                    }
                    if (mouse->key_states[rawinput::MOUSEBTN_MIDDLE]) {
                        mouse_state[ImGuiMouseButton_Middle] = true;
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

                        if (state) {
                            KeysDownNew[vKey] = ~0;
                        }

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

    // merge modifier keys
    KeysDownNew[VK_CONTROL] |= KeysDownNew[VK_LCONTROL] | KeysDownNew[VK_RCONTROL];
    KeysDownNew[VK_MENU] |= KeysDownNew[VK_LMENU] | KeysDownNew[VK_RSHIFT];
    KeysDownNew[VK_SHIFT] |= KeysDownNew[VK_LSHIFT] | KeysDownNew[VK_RSHIFT];

    // combined keys
    {
        static struct {
            ImGuiKey index;
            UINT vKey;
        } KEY_MAPPING[] = {
            { ImGuiKey_Tab, VK_TAB },
            { ImGuiKey_LeftArrow, VK_LEFT },
            { ImGuiKey_RightArrow, VK_RIGHT },
            { ImGuiKey_UpArrow, VK_UP },
            { ImGuiKey_DownArrow, VK_DOWN },
            { ImGuiKey_PageUp, VK_PRIOR },
            { ImGuiKey_PageDown, VK_NEXT },
            { ImGuiKey_Home, VK_HOME },
            { ImGuiKey_End, VK_END },
            { ImGuiKey_Insert, VK_INSERT },
            { ImGuiKey_Delete, VK_DELETE },
            { ImGuiKey_Backspace, VK_BACK },
            { ImGuiKey_Space, VK_SPACE },
            { ImGuiKey_Enter, VK_RETURN },
            { ImGuiKey_Escape, VK_ESCAPE },
            { ImGuiKey_KeypadEnter, VK_RETURN },
            { ImGuiKey_A, 'A' },
            { ImGuiKey_C, 'C' },
            { ImGuiKey_V, 'V' },
            { ImGuiKey_X, 'X' },
            { ImGuiKey_Y, 'Y' },
            { ImGuiKey_Z, 'Z' },
            { ImGuiMod_Ctrl, VK_CONTROL },
            { ImGuiMod_Alt, VK_MENU },
            { ImGuiMod_Shift, VK_SHIFT },
        };
        for (const auto& mapping : KEY_MAPPING) {
            if ((KeysDownNew[mapping.vKey] != 0) != (KeysDownOld[mapping.vKey] != 0)) {
                io.AddKeyEvent(mapping.index, (KeysDownNew[mapping.vKey] != 0));
            }
        }
    }

    // remember current key state for next frame
    memcpy(KeysDownOld, KeysDownNew, sizeof(KeysDownOld));
    // needed for ToAscii to correctly detect caps lock state
    KeysDownOld[VK_CAPITAL] = ::GetKeyState(VK_CAPITAL) & 0x0001;

    // update OS mouse position
    bool touch_down = false;
    ImGui_ImplSpice_UpdateMousePos(&touch_down);
    if (touch_down) {
        mouse_state[ImGuiMouseButton_Left] = true;
    }

    // combined mouse
    for (size_t i = 0; i < std::size(mouse_state); i++) {
        if (mouse_state[i] != io.MouseDown[i]) {
            io.AddMouseButtonEvent(i, mouse_state[i]);
        }
    }

    // set mouse wheel
    auto mouse_diff = mouse_wheel - mouse_wheel_last;
    mouse_wheel_last = mouse_wheel;
    io.AddMouseWheelEvent(0, mouse_diff);

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
