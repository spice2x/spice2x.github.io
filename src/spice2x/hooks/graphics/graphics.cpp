#include <initguid.h>

#include "graphics.h"

#include <set>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "avs/game.h"
#include "cfg/icon.h"
#include "cfg/screen_resize.h"
#include "games/ddr/ddr.h"
#include "games/iidx/iidx.h"
#include "hooks/graphics/backends/d3d9/d3d9_backend.h"
#include "launcher/shutdown.h"
#include "overlay/overlay.h"
#include "touch/touch.h"
#include "touch/touch_indicators.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/fileutils.h"
#include "util/utils.h"
#include "misc/wintouchemu.h"
#include "util/time.h"
#include "rawinput/rawinput.h"

struct CaptureData {
    std::shared_ptr<uint8_t[]> data;
    unsigned short width, height;
    uint64_t timestamp;
};

HWND TDJ_SUBSCREEN_WINDOW = nullptr;
HWND SDVX_SUBSCREEN_WINDOW = nullptr;

// icon
static HICON WINDOW_ICON = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(MAINICON));

// state
static WNDPROC WNDPROC_ORIG = nullptr;
static WNDPROC WSUB_WNDPROC_ORIG = nullptr;
static std::vector<WNDPROC> WNDPROC_CUSTOM {};
static bool GRAPHICS_SCREENSHOT_TRIGGER = false;
static std::set<int> GRAPHICS_SCREENS { 0 };
static std::mutex GRAPHICS_SCREENS_M {};
static std::vector<int> GRAPHICS_CAPTURE_SCREENS;
static const size_t GRAPHICS_CAPTURE_SCREEN_NO = 4;
static std::mutex GRAPHICS_CAPTURE_SCREENS_M {};
static CaptureData GRAPHICS_CAPTURE_BUFFER[GRAPHICS_CAPTURE_SCREEN_NO] {};
static std::mutex GRAPHICS_CAPTURE_BUFFER_M[GRAPHICS_CAPTURE_SCREEN_NO] {};
static std::condition_variable GRAPHICS_CAPTURE_CV[GRAPHICS_CAPTURE_SCREEN_NO] {};

// flag settings
bool GRAPHICS_CAPTURE_CURSOR = false;
bool GRAPHICS_LOG_HRESULT = false;
bool GRAPHICS_SDVX_FORCE_720 = false;
bool GRAPHICS_SHOW_CURSOR = false;
graphics_orientation GRAPHICS_ADJUST_ORIENTATION = ORIENTATION_NORMAL;
bool GRAPHICS_WINDOWED = false;
std::vector<HWND> GRAPHICS_WINDOWS;
UINT GRAPHICS_FORCE_REFRESH = 0;
std::optional<int> GRAPHICS_FORCE_VSYNC_BUFFER;
bool GRAPHICS_FORCE_SINGLE_ADAPTER = false;
bool GRAPHICS_PREVENT_SECONDARY_WINDOW = false;
graphics_dx9on12_state GRAPHICS_9_ON_12_STATE = DX9ON12_AUTO;
bool GRAPHICS_9_ON_12_REQUESTED_BY_GAME = false;
bool SUBSCREEN_FORCE_REDRAW = false;
bool D3D9_DEVICE_HOOK_DISABLE = false;
std::optional<std::pair<uint32_t, uint32_t>> GRAPHICS_FS_CUSTOM_RESOLUTION;

// settings
std::string GRAPHICS_DEVICEID = "PCI\\VEN_1002&DEV_7146";
std::string GRAPHICS_SCREENSHOT_DIR = ".\\screenshots";

static decltype(ChangeDisplaySettingsA) *ChangeDisplaySettingsA_orig = nullptr;
static decltype(ChangeDisplaySettingsExA) *ChangeDisplaySettingsExA_orig = nullptr;
static decltype(ClipCursor) *ClipCursor_orig = nullptr;
static decltype(CreateWindowExA) *CreateWindowExA_orig = nullptr;
static decltype(CreateWindowExW) *CreateWindowExW_orig = nullptr;
static decltype(EnableWindow) *EnableWindow_orig = nullptr;
static decltype(EnumDisplayDevicesA) *EnumDisplayDevicesA_orig = nullptr;
static decltype(MoveWindow) *MoveWindow_orig = nullptr;
static decltype(PeekMessageA) *PeekMessageA_orig = nullptr;
static decltype(RegisterClassA) *RegisterClassA_orig = nullptr;
static decltype(RegisterClassExA) *RegisterClassExA_orig = nullptr;
static decltype(RegisterClassW) *RegisterClassW_orig = nullptr;
static decltype(RegisterClassExW) *RegisterClassExW_orig = nullptr;
static decltype(ShowCursor) *ShowCursor_orig = nullptr;
static decltype(SetCursor) *SetCursor_orig = nullptr;
static decltype(SetWindowLongA) *SetWindowLongA_orig = nullptr;
static decltype(SetWindowLongW) *SetWindowLongW_orig = nullptr;
static decltype(SetWindowPos) *SetWindowPos_orig = nullptr;

static void reset_window_hook(HWND hWnd) {
    overlay::destroy(hWnd);

    if (WNDPROC_ORIG) {
        SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (LONG_PTR) WNDPROC_ORIG);
        WNDPROC_ORIG = nullptr;
    }
}

// window procedure
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    // terminate
    if (uMsg == WM_CLOSE) {
        log_info("graphics", "detected WM_CLOSE, terminating...");
        launcher::shutdown(0);
        return false;
    }

    // overlay specific
    if (overlay::OVERLAY) {
        switch (uMsg) {
            case WM_CHAR: {

                // input characters if overlay is active
                if (overlay::OVERLAY->has_focus()) {
                    overlay::OVERLAY->input_char((unsigned int) wParam);
                    return true;
                }
                break;
            }
            case WM_DESTROY: {
                auto wndproc = WNDPROC_ORIG;
                reset_window_hook(hWnd);
                return CallWindowProcA(wndproc, hWnd, uMsg, wParam, lParam);
            }
            case WM_SETCURSOR: {

                // set cursor back to the overlay one
                if (LOWORD(lParam) == HTCLIENT && overlay::OVERLAY->update_cursor()) {
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    if (wintouchemu::INJECT_MOUSE_AS_WM_TOUCH) {
        // drop mouse inputs since only wintouches should be used
        switch (uMsg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                return true;
        }
    }

    // window resize
    graphics_windowed_wndproc(hWnd, uMsg, wParam, lParam);

    // call custom procedures
    for (WNDPROC wndProc : WNDPROC_CUSTOM) {
        wndProc(hWnd, uMsg, wParam, lParam);
    }

    // capture mouse
    if (GRAPHICS_CAPTURE_CURSOR) {
        bool free_cursor = false;
        bool capture_cursor = false;
        bool early_return = false;
        switch (uMsg) {
            case WM_SETFOCUS:
                capture_cursor = true;
                early_return = true;
                break;
            case WM_KILLFOCUS:
                free_cursor = true;
                early_return = true;
                break;
            case WM_WINDOWPOSCHANGED:
                // known issue: dragging with the title bar results in WM_WINDOWPOSCHANGED
                // getting called, which calls ClipCursor successfully, but doesn't actually
                // confine the cursor for some reason; seems like odd Windows behavior
                // (can be fixed if focus is shifted to another window and then back to the game
                // window)
                if (hWnd == GetActiveWindow()) {
                    capture_cursor = true;
                } else {
                    free_cursor = true;
                }
                // do not return early; may result in WM_SIZE / WM_MOVE no longer being called
            default:
                break;
        }

        if (free_cursor) {
            ClipCursor(nullptr);
        } else if (capture_cursor) {
            RECT WINDOW_RECT;
            GetWindowRect(hWnd, &WINDOW_RECT);
            ClipCursor(&WINDOW_RECT);
        }
        if (early_return) {
            return true;
        }
    }

    // drop keydown messages
    switch (uMsg) {
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
            return true;
        default:
            break;
    }

    switch (uMsg) {
        case WM_MOVE:
        case WM_SIZE: {
            // Update SPICETOUCH space when the main window changes size or moves.
            // The update happens regardless of whether the "fake" spicetouch window is present or not.
            // This allows touches received on subscreen window to be translated correctly.
            update_spicetouch_window_dimensions(hWnd);
            // log_misc(
            //     "graphics", "detected window change ({}x{} @ {}, {}), updating touch coord-space to match", 
            //     SPICETOUCH_TOUCH_WIDTH, SPICETOUCH_TOUCH_HEIGHT, SPICETOUCH_TOUCH_X, SPICETOUCH_TOUCH_Y);
                
            // Update SPICETOUCH window if present
            if (SPICETOUCH_TOUCH_HWND) {
                SetWindowPos(
                    SPICETOUCH_TOUCH_HWND, HWND_TOP,
                    SPICETOUCH_TOUCH_X, SPICETOUCH_TOUCH_Y,
                    SPICETOUCH_TOUCH_WIDTH, SPICETOUCH_TOUCH_HEIGHT,
                    SWP_NOZORDER | SWP_NOREDRAW | SWP_NOREPOSITION | SWP_NOACTIVATE);
            }
        }
        default:
            break;
    }

    // call default
    return CallWindowProcA(WNDPROC_ORIG, hWnd, uMsg, wParam, lParam);
}

// window procedure for subscreen
// this might be replaced by spicetouch hook later
static LRESULT CALLBACK WsubWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CLOSE) {
        log_misc("graphics", "ignore WM_CLOSE for subscreen window");
        return false;
    }
    return CallWindowProcA(WSUB_WNDPROC_ORIG, hWnd, uMsg, wParam, lParam);
}

static LONG WINAPI ChangeDisplaySettingsA_hook(DEVMODEA *lpDevMode, DWORD dwflags) {
    log_misc("graphics", "ChangeDisplaySettingsA hook hit");

    // ignore display settings changes when running windowed
    if (GRAPHICS_WINDOWED) {
        return DISP_CHANGE_SUCCESSFUL;
    }

    // call original
    return ChangeDisplaySettingsA_orig(lpDevMode, dwflags);
}

static LONG WINAPI ChangeDisplaySettingsExA_hook(LPCSTR lpszDeviceName, DEVMODEA *lpDevMode, HWND hwnd,
        DWORD dwflags, LPVOID lParam)
{
    log_misc("graphics", "ChangeDisplaySettingsExA hook hit");

    // ignore display settings changes when running windowed
    if (GRAPHICS_WINDOWED) {
        return DISP_CHANGE_SUCCESSFUL;
    }

    // call original
    return ChangeDisplaySettingsExA_orig(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
}

static BOOL WINAPI ClipCursor_hook(const RECT *lpRect) {
    log_misc("graphics", "ClipCursor hook hit");

    // ignore cursor confine when having no explicit cursor confine
    if (!GRAPHICS_CAPTURE_CURSOR) {
        return TRUE;
    }

    // call original
    return ClipCursor_orig(lpRect);
}

static HWND WINAPI CreateWindowExA_hook(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
                                        DWORD dwStyle, int x, int y, int nWidth, int nHeight,
                                        HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                                        LPVOID lpParam)
{
    const std::string window_name(lpWindowName != nullptr ? lpWindowName : "(null)");

    log_misc("graphics", "CreateWindowExA hook hit (0x{:08x}, {}, {}, 0x{:08x}, {}, {}, {}, {}, {}, {}, {}, {})",
            dwExStyle,
            fmt::ptr(lpClassName),
            window_name,
            dwStyle,
            x,
            y,
            nWidth,
            nHeight,
            fmt::ptr(hWndParent),
            fmt::ptr(hMenu),
            fmt::ptr(hInstance),
            fmt::ptr(lpParam));

    // set display orientation and/or refresh rate
    // only set orientation when the target window is portrait
    // (avoid doing this for SDVX subscreen which is in landscape, for example)
    const auto adjust_orientation =
        (GRAPHICS_ADJUST_ORIENTATION == ORIENTATION_CW ||
         GRAPHICS_ADJUST_ORIENTATION == ORIENTATION_CCW);

    if ((nHeight > nWidth && adjust_orientation) || GRAPHICS_FORCE_REFRESH > 0) {
        DEVMODE mode {};

        // get display settings
        if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
            if (adjust_orientation) {
                DWORD orientation = GRAPHICS_ADJUST_ORIENTATION == ORIENTATION_CW ? DMDO_90 : DMDO_270;
                log_misc(
                    "graphics",
                    "auto-rotate: call ChangeDisplaySettings and rotate display to DMDO_xx mode {}",
                    orientation);
                mode.dmPelsWidth = nWidth;
                mode.dmPelsHeight = nHeight;
                mode.dmDisplayOrientation = orientation;
            }

            if (GRAPHICS_FORCE_REFRESH > 0) {
                log_info(
                    "graphics",
                    "call ChangeDisplaySettings to force refresh rate: {} => {} Hz (-graphics-force-refresh)",
                    mode.dmDisplayFrequency,
                    GRAPHICS_FORCE_REFRESH);

                mode.dmDisplayFrequency = GRAPHICS_FORCE_REFRESH;
            }

            const auto disp_res = ChangeDisplaySettings(&mode, CDS_FULLSCREEN);
            if (disp_res != DISP_CHANGE_SUCCESSFUL) {
                log_warning("graphics", "failed to change display settings: {}", disp_res);
            }
        } else {
            log_warning("graphics", "failed to get display settings");
        }
    }

    // gfdm
    if (avs::game::is_model({"J32", "J33", "K32", "K33", "L32", "L33", "M32"})) {
        // set window name
        if (!lpWindowName) {
            lpWindowName = "GITADORA";
        }
    }

    bool is_tdj_sub_window = avs::game::is_model("LDJ") && window_name.ends_with(" sub");
    bool is_sdvx_sub_window = avs::game::is_model("KFC") && window_name.ends_with(" Sub Screen");

    // TDJ windowed mode with subscreen: hide maximize button
    if ((is_tdj_sub_window && GRAPHICS_IIDX_WSUB) || is_sdvx_sub_window) {
        dwStyle &= ~(WS_MAXIMIZEBOX);
    }

    // call original
    HWND result = CreateWindowExA_orig(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight,
            hWndParent, hMenu, hInstance, lpParam);
    GRAPHICS_WINDOWS.push_back(result);

    if (is_tdj_sub_window) {
        // TDJ windowed mode: remember the subscreen window handle for later
        TDJ_SUBSCREEN_WINDOW = result;
    
        // hook for preventing the closing of subscreen window
        if (GRAPHICS_IIDX_WSUB) {
            graphics_hook_subscreen_window(TDJ_SUBSCREEN_WINDOW);
        }
    }

    // hook for preventing the closing of subscreen window
    if (is_sdvx_sub_window) {
        SDVX_SUBSCREEN_WINDOW = result;
        graphics_hook_subscreen_window(SDVX_SUBSCREEN_WINDOW);
    }

    disable_touch_indicators(result);
    return result;
}

static HWND WINAPI CreateWindowExW_hook(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
                                        DWORD dwStyle, int x, int y, int nWidth, int nHeight,
                                        HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                                        LPVOID lpParam)
{
    log_misc("graphics", "CreateWindowExW hook hit ({:x}, {}, {}, {:x}, {}, {}, {}, {}, {}, {}, {}, {})",
            dwExStyle,
            fmt::ptr(lpClassName),
            lpWindowName != nullptr ? ws2s(lpWindowName) : "(null)",
            dwStyle,
            x,
            y,
            nWidth,
            nHeight,
            fmt::ptr(hWndParent),
            fmt::ptr(hMenu),
            fmt::ptr(hInstance),
            fmt::ptr(lpParam));

    // DDR specific stuff
    if (avs::game::is_model("MDX")) {

        // set window name
        if (!lpWindowName) {
            lpWindowName = L"Dance Dance Revolution";
        }

        // windowed mode adjustments
        if (GRAPHICS_WINDOWED) {

            // change window style
            dwExStyle = 0;
            dwStyle |= WS_OVERLAPPEDWINDOW;

            // adjust window size to include window decoration
            RECT rect {};
            if (games::ddr::SDMODE) {
                SetRect(&rect, 0, 0, 800, 600);
            } else {
                SetRect(&rect, 0, 0, 1280, 720);
            }
            AdjustWindowRect(&rect, dwStyle, (hMenu != nullptr));

            nWidth = rect.right - rect.left;
            nHeight = rect.bottom - rect.top;
        }
    }

    // DanEvo specific stuff
    if (avs::game::is_model("KDM")) {

        // set window name
        if (!lpWindowName) {
            lpWindowName = L"Dance Evolution";
        }
    }

    // call original
    HWND result = CreateWindowExW_orig(
            dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight,
            hWndParent, hMenu, hInstance, lpParam);
    GRAPHICS_WINDOWS.push_back(result);

    disable_touch_indicators(result);
    return result;
}

static BOOL WINAPI EnableWindow_hook(HWND hWnd, BOOL bEnable) {
    return TRUE;
}

static BOOL WINAPI EnumDisplayDevicesA_hook(LPCTSTR lpDevice, DWORD iDevNum,
        PDISPLAY_DEVICE lpDisplayDevice, DWORD dwFlags) {

    // call original
    BOOL value = EnumDisplayDevicesA_orig(lpDevice, iDevNum, lpDisplayDevice, dwFlags);

#ifndef SPICE64
    // older IIDX games check for hardcoded PCI vendor/device ID pair of GPU
    if ((avs::game::is_model("JDZ") || avs::game::is_model("KDZ")) && value) {
        log_info(
            "graphics",
            "EnumDisplayDevicesA_hook: swap DeviceID {} with {} (for IIDX 18/19)",
            lpDisplayDevice->DeviceID,
            GRAPHICS_DEVICEID.c_str());
        memcpy(&lpDisplayDevice->DeviceID, GRAPHICS_DEVICEID.c_str(), GRAPHICS_DEVICEID.size() + 1);
    }
#endif

    // return original result
    return value;
}

static BOOL WINAPI MoveWindow_hook(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) {
    log_misc("graphics", "MoveWindow hook hit ({}, {}, {}, {}, {}, {})",
        fmt::ptr(hWnd),
        X,
        Y,
        nWidth,
        nHeight,
        bRepaint);

    // sound voltex windowed mode adjustments
    if (GRAPHICS_WINDOWED && GRAPHICS_SDVX_FORCE_720 && avs::game::is_model("KFC")) {
        RECT rect {};
        DWORD dwStyle;

        dwStyle = GetWindowLongA(hWnd, GWL_STYLE);

        // luckily, SDVX does not draw a menu. So we can leave the last
        // argument to `AdjustWindowRect` as `0`.
        SetRect(&rect, 0, 0, 720, 1280);
        AdjustWindowRect(&rect, dwStyle, 0);

        nWidth = rect.right - rect.left;
        nHeight = rect.bottom - rect.top;
    }

    // iidx windowed TDJ mode
    if (GRAPHICS_WINDOWED && TDJ_SUBSCREEN_WINDOW && hWnd == TDJ_SUBSCREEN_WINDOW) {
       if (GRAPHICS_IIDX_WSUB) {
           // (Experimental) Show subscreen in windowed mode
            graphics_load_windowed_subscreen_parameters();
 
            RECT rect {};
            DWORD dwStyle;

            dwStyle = GetWindowLongA(hWnd, GWL_STYLE);

            SetRect(&rect, 0, 0, GRAPHICS_IIDX_WSUB_WIDTH, GRAPHICS_IIDX_WSUB_HEIGHT);
            AdjustWindowRect(&rect, dwStyle, 0);

            X = GRAPHICS_IIDX_WSUB_X;
            Y = GRAPHICS_IIDX_WSUB_Y;

            nWidth = rect.right - rect.left;
            nHeight = rect.bottom - rect.top;

            touch_attach_wnd(TDJ_SUBSCREEN_WINDOW);
        } else {
            // Existing behaviour: suppress subscreen window and prompt user to use overlay instead
             log_info(
                "graphics",
                "MoveWindow hook - hiding TDJ subscreen window {}; please use subscreen overlay instead (-iidxtdjw)",
                fmt::ptr(hWnd));
            SendMessage(hWnd, WM_CLOSE, 0, 0);
            TDJ_SUBSCREEN_WINDOW = nullptr;
            return TRUE;
        }
    }

    // call original
    return MoveWindow_orig(hWnd, X, Y, nWidth, nHeight, bRepaint);
}

static BOOL WINAPI PeekMessageA_hook(
        LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {

    // SDVX polls for messages too slowly
    if (avs::game::is_model("KFC")) {

        // process remaining messages
        BOOL ret;
        while ((ret = PeekMessageA_orig(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE)) != 0) {
            if (ret == -1) {
                return ret;
            } else {
                TranslateMessage(lpMsg);
                DispatchMessageA(lpMsg);
            }
        }

        // return no message
        return FALSE;
    }

    return PeekMessageA_orig(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

static int WINAPI ShowCursor_hook(BOOL bShow) {

    // prevent game from hiding cursor when option is enabled
    if (GRAPHICS_SHOW_CURSOR && !bShow) {
        return 1;
    }

    // call original
    return ShowCursor_orig(bShow);
}

static HCURSOR WINAPI SetCursor_hook(HCURSOR hCursor) {
    if (GRAPHICS_SHOW_CURSOR && hCursor == NULL) {
        return GetCursor();
    }
    return SetCursor_orig(hCursor);
}

static LONG WINAPI SetWindowLongA_hook(HWND hWnd, int nIndex, LONG dwNewLong) {

    // DDR window style fix
    if (nIndex == GWL_STYLE && avs::game::is_model("MDX")) {
        dwNewLong |= WS_OVERLAPPEDWINDOW;
    }

    // call original
    return SetWindowLongA_orig(hWnd, nIndex, dwNewLong);
}

static LONG WINAPI SetWindowLongW_hook(HWND hWnd, int nIndex, LONG dwNewLong) {

    // DDR overlapped window fix
    if (nIndex == GWL_STYLE && avs::game::is_model("MDX")) {
        dwNewLong |= WS_OVERLAPPEDWINDOW;
    }

    // call original
    return SetWindowLongW_orig(hWnd, nIndex, dwNewLong);
}

static BOOL WINAPI SetWindowPos_hook(HWND hWnd, HWND hWndInsertAfter,
        int X, int Y, int cx, int cy, UINT uFlags) {

    // windowed mode adjustments
    if (GRAPHICS_WINDOWED && (avs::game::is_model("LMA") || avs::game::is_model("MDX"))) {
        return TRUE;
    }

    // call original
    return SetWindowPos_orig(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static ATOM WINAPI RegisterClassA_hook(const WNDCLASSA *lpWndClass) {

    // check for null
    if (!lpWndClass) {
        return RegisterClassA_orig(lpWndClass);
    }

    // copy struct and use own icon
    WNDCLASSA wnd = *lpWndClass;
    wnd.hIcon = WINDOW_ICON;

    // call original
    return RegisterClassA_orig(&wnd);
}

static ATOM WINAPI RegisterClassExA_hook(const WNDCLASSEXA *Arg1) {

    // check for null
    if (!Arg1) {
        return RegisterClassExA_orig(Arg1);
    }

    // copy struct and use own icon
    WNDCLASSEXA wnd = *Arg1;
    wnd.hIcon = WINDOW_ICON;
    wnd.hIconSm = WINDOW_ICON;

    // call original
    return RegisterClassExA_orig(&wnd);
}

static ATOM WINAPI RegisterClassW_hook(const WNDCLASSW *lpWndClass) {

    // check for null
    if (!lpWndClass) {
        return RegisterClassW_orig(lpWndClass);
    }

    // copy struct and use own icon
    WNDCLASSW wnd = *lpWndClass;
    wnd.hIcon = WINDOW_ICON;

    // call original
    return RegisterClassW_orig(&wnd);
}

static ATOM WINAPI RegisterClassExW_hook(const WNDCLASSEXW *Arg1) {

    // check for null
    if (!Arg1) {
        return RegisterClassExW_orig(Arg1);
    }

    // copy struct and use own icon
    WNDCLASSEXW wnd = *Arg1;
    wnd.hIcon = WINDOW_ICON;
    wnd.hIconSm = WINDOW_ICON;

    // call original
    return RegisterClassExW_orig(&wnd);
}

static HHOOK WINAPI SetWindowsHookExA_hook(int, HOOKPROC, HINSTANCE, DWORD) {
    log_misc("graphics", "SetWindowsHookExA hook hit");

    // we don't do hooks
    return nullptr;
}

static BOOL WINAPI SetCursorPos_hook(int, int) {

    // prevent games from messing with the cursor position themselves
    return TRUE;
}

static int WINAPI MessageBoxA_hook(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {

    auto text = lpText != nullptr ? lpText : "(null)";
    auto title = lpCaption != nullptr ? lpCaption : "(null)";

    log_info("graphics", "MessageBoxA: {} - {}", title, text);
    return IDOK;
}

static int WINAPI MessageBoxExA_hook(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType, WORD wLanguageId) {

    auto text = lpText != nullptr ? lpText : "(null)";
    auto title = lpCaption != nullptr ? lpCaption : "(null)";

    log_info("graphics", "MessageBoxExA: {} - {}", title, text);
    return IDOK;
}

static int WINAPI MessageBoxW_hook(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) {

    auto text = lpText != nullptr ? lpText : L"(null)";
    auto title = lpCaption != nullptr ? lpCaption : L"(null)";

    log_info("graphics", "MessageBoxW: {} - {}", ws2s(title), ws2s(text));
    return IDOK;
}

static int WINAPI MessageBoxExW_hook(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType, WORD wLanguageId) {

    auto text = lpText != nullptr ? lpText : L"(null)";
    auto title = lpCaption != nullptr ? lpCaption : L"(null)";

    log_info("graphics", "MessageBoxExW: {} - {}", ws2s(title), ws2s(text));
    return IDOK;
}

void graphics_init() {
    log_info("graphics", "initializing");

    // init screen resize
    log_info("ScreenResize", "initializing");
    if(cfg::SCREENRESIZE == nullptr){
        cfg::SCREENRESIZE = std::make_unique<cfg::ScreenResize>();
    }

    // init backends
    graphics_d3d9_init();

    // general hooks
    ChangeDisplaySettingsA_orig = detour::iat_try("ChangeDisplaySettingsA", ChangeDisplaySettingsA_hook);
    ChangeDisplaySettingsExA_orig = detour::iat_try("ChangeDisplaySettingsExA", ChangeDisplaySettingsExA_hook);
    ClipCursor_orig = detour::iat_try("ClipCursor", ClipCursor_hook);
    CreateWindowExA_orig = detour::iat_try("CreateWindowExA", CreateWindowExA_hook);
    CreateWindowExW_orig = detour::iat_try("CreateWindowExW", CreateWindowExW_hook);
    EnableWindow_orig = detour::iat_try("EnableWindow", EnableWindow_hook);
    EnumDisplayDevicesA_orig = detour::iat_try("EnumDisplayDevicesA", EnumDisplayDevicesA_hook);
    MoveWindow_orig = detour::iat_try("MoveWindow", MoveWindow_hook);
    PeekMessageA_orig = detour::iat_try("PeekMessageA", PeekMessageA_hook);
    RegisterClassA_orig = detour::iat_try("RegisterClassA", RegisterClassA_hook);
    RegisterClassExA_orig = detour::iat_try("RegisterClassExA", RegisterClassExA_hook);
    RegisterClassW_orig = detour::iat_try("RegisterClassW", RegisterClassW_hook);
    RegisterClassExW_orig = detour::iat_try("RegisterClassExW", RegisterClassExW_hook);
    ShowCursor_orig = detour::iat_try("ShowCursor", ShowCursor_hook);
    SetCursor_orig = detour::iat_try("SetCursor", SetCursor_hook);
    SetWindowLongA_orig = detour::iat_try("SetWindowLongA", SetWindowLongA_hook);
    SetWindowLongW_orig = detour::iat_try("SetWindowLongW", SetWindowLongW_hook);
    SetWindowPos_orig = detour::iat_try("SetWindowPos", SetWindowPos_hook);

    detour::iat_try("MessageBoxA", MessageBoxA_hook);
    detour::iat_try("MessageBoxExA", MessageBoxExA_hook);
    detour::iat_try("MessageBoxW", MessageBoxW_hook);
    detour::iat_try("MessageBoxExW", MessageBoxExW_hook);

    detour::iat_try("SetWindowsHookExA", SetWindowsHookExA_hook);
    detour::iat_try("SetCursorPos", SetCursorPos_hook);
}

void graphics_hook_window(HWND hWnd, D3DPRESENT_PARAMETERS *pPresentationParameters) {

    // update window size for a few games
    // TODO: make this work on everything
    if (pPresentationParameters != nullptr && GRAPHICS_WINDOWED
    && (avs::game::is_model({ "K39", "L39", "M39", "JMP", "LDJ" }))) {

        // check dimensions
        auto new_width = pPresentationParameters->BackBufferWidth;
        auto new_height = pPresentationParameters->BackBufferHeight;
        if (new_width != 0 && new_height != 0) {
            RECT rect {};
            GetWindowRect(hWnd, &rect);

            auto width = rect.right - rect.left;
            auto height = rect.bottom - rect.top;

            log_info("graphics", "resized window: {}x{} -> {}x{}", width, height, new_width, new_height);

            DWORD dwStyle = GetWindowLongA(hWnd, GWL_STYLE);
            DWORD dwExStyle = GetWindowLongA(hWnd, GWL_EXSTYLE);
            HMENU menu = GetMenu(hWnd);
            SetRect(&rect, 0, 0, new_width, new_height);
            AdjustWindowRectEx(&rect, dwStyle, (menu != nullptr), dwExStyle);

            // make sure the window does not go off the screen
            if (rect.top < 0) {
                rect.bottom += -rect.top;
                rect.top = 0;
            }

            width = rect.right - rect.left;
            height = rect.bottom - rect.top;

            SetWindowPos(hWnd, HWND_TOP, rect.left, rect.top, width, height, 0);
        }
    }

    // show cursor
    if (GRAPHICS_SHOW_CURSOR) {
        ShowCursor(TRUE);
    }

    // capture mouse
    if (GRAPHICS_CAPTURE_CURSOR) {
        RECT rect {};
        GetWindowRect(hWnd, &rect);
        ClipCursor(&rect);
    }

    // hook window procedure
    if (WNDPROC_ORIG == nullptr) {
        WNDPROC_ORIG = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hWnd, GWLP_WNDPROC));
        SetWindowLongPtrA(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc));

        // NOLEGACY causes WM_CHAR to be not received
        // reflec beat game engine does not pass WM_CHAR through for some reason (unrelated to SpiceTouch)
        if (!rawinput::NOLEGACY && !(avs::game::is_model({"KBR", "LBR", "MBR"}))) {
            overlay::USE_WM_CHAR_FOR_IMGUI_CHAR_INPUT = true;
        }
        graphics_capture_initial_window(hWnd);
    }
}

void graphics_add_wnd_proc(WNDPROC wnd_proc) {
    WNDPROC_CUSTOM.push_back(wnd_proc);
}

void graphics_remove_wnd_proc(WNDPROC wndProc) {
    for (size_t x = 0; x < WNDPROC_CUSTOM.size(); x++) {
        if (WNDPROC_CUSTOM[x] == wndProc) {
            WNDPROC_CUSTOM.erase(WNDPROC_CUSTOM.begin() + x);
        }
    }
}

void graphics_hook_subscreen_window(HWND hWnd) {
    // hook window procedure
    if (WSUB_WNDPROC_ORIG == nullptr) {
        WSUB_WNDPROC_ORIG = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hWnd, GWLP_WNDPROC));
        SetWindowLongPtrA(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WsubWindowProc));
    }
}

void graphics_screens_register(int screen) {
    std::lock_guard<std::mutex> lock(GRAPHICS_SCREENS_M);

    GRAPHICS_SCREENS.insert(screen);
}

void graphics_screens_unregister(int screen) {
    std::lock_guard<std::mutex> lock(GRAPHICS_SCREENS_M);

    GRAPHICS_SCREENS.erase(screen);
}

void graphics_screens_get(std::vector<int> &screens) {
    std::lock_guard<std::mutex> lock(GRAPHICS_SCREENS_M);

    screens.insert(screens.end(), GRAPHICS_SCREENS.begin(), GRAPHICS_SCREENS.end());
}

void graphics_screenshot_trigger() {
    GRAPHICS_SCREENSHOT_TRIGGER = true;
}

bool graphics_screenshot_consume() {
    auto flag = GRAPHICS_SCREENSHOT_TRIGGER;
    GRAPHICS_SCREENSHOT_TRIGGER = false;
    return flag;
}

void graphics_capture_trigger(int screen) {
    std::lock_guard<std::mutex> lock(GRAPHICS_CAPTURE_SCREENS_M);

    GRAPHICS_CAPTURE_SCREENS.push_back(screen);
}

bool graphics_capture_consume(int *screen) {
    auto flag = !GRAPHICS_CAPTURE_SCREENS.empty();
    if (flag) {
        std::lock_guard<std::mutex> lock(GRAPHICS_CAPTURE_SCREENS_M);

        *screen = GRAPHICS_CAPTURE_SCREENS.back();
        GRAPHICS_CAPTURE_SCREENS.pop_back();
    }
    return flag;
}

void graphics_capture_enqueue(int screen, uint8_t *data, size_t width, size_t height) {
    GRAPHICS_CAPTURE_BUFFER_M[screen].lock();
    auto &capture = GRAPHICS_CAPTURE_BUFFER[screen];
    capture.data.reset(data);
    capture.width = width;
    capture.height = height;
    capture.timestamp = get_performance_milliseconds();
    GRAPHICS_CAPTURE_BUFFER_M[screen].unlock();
    GRAPHICS_CAPTURE_CV[screen].notify_one();
}

void graphics_capture_skip(int screen) {
    GRAPHICS_CAPTURE_CV[screen].notify_one();
}

bool graphics_capture_receive_jpeg(int screen, TooJpeg::WRITE_ONE_BYTE receiver,
        bool rgb, int quality, bool downsample, int divide, uint64_t *timestamp,
        int *width, int *height) {

    // wait for capture event
    std::unique_lock<std::mutex> lock(GRAPHICS_CAPTURE_BUFFER_M[screen]);
    GRAPHICS_CAPTURE_CV[screen].wait(lock, [screen] {
        return GRAPHICS_CAPTURE_BUFFER[screen].data != nullptr;
    });
    auto &capture = GRAPHICS_CAPTURE_BUFFER[screen];
    auto capture_data = capture.data;
    auto capture_width = capture.width;
    auto capture_height = capture.height;
    auto capture_timestamp = capture.timestamp;
    lock.unlock();

    // validate data
    if (!capture_data || !capture_width || !capture_height) {
        return false;
    }

    // divide image size
    if (divide > 1) {

        // get new resolution (round up)
        int width_new = (capture_width + divide - 1) / divide;
        int height_new = (capture_height + divide - 1) / divide;

        // allocate new data
        auto data_old = capture_data.get();
        auto data_new = new uint8_t[width_new * height_new * 3];

        // copy pixel data
        int data_y = 0;
        for (int y = 0; y < capture_height; y += divide) {
            int data_y_offset_old = y * capture_width;
            int data_y_offset = data_y * width_new;
            int data_x = 0;
            for (int x = 0; x < capture_width; x += divide) {
                auto pixel_new = &data_new[(data_x + data_y_offset) * 3];
                auto pixel_old = &data_old[(data_y_offset_old + x) * 3];
                memcpy(pixel_new, pixel_old, 3);
                data_x++;
            }
            data_y++;
        }

        // update capture data
        capture_data.reset(data_new);
        capture_width = width_new;
        capture_height = height_new;
    }

    // compress
    auto success = TooJpeg::writeJpeg(
            receiver, capture_data.get(),
            capture_width, capture_height,
            rgb, quality, downsample);

    // status
    if (timestamp) {
        *timestamp = capture_timestamp;
    }
    if (width) {
        *width = capture_width;
    }
    if (height) {
        *height = capture_height;
    }

    // clean up
    return success;
}

std::string graphics_screenshot_genpath() {

    // verify dir path
    if (GRAPHICS_SCREENSHOT_DIR.empty()) {
        return "";
    } else {
        auto last_char = GRAPHICS_SCREENSHOT_DIR.back();
        if (last_char == '\\' || last_char == '/') {
            GRAPHICS_SCREENSHOT_DIR.pop_back();
        }
    }

    // ensure the output directory exists
    if (!fileutils::dir_exists(GRAPHICS_SCREENSHOT_DIR)) {
        if (!fileutils::dir_create_recursive(GRAPHICS_SCREENSHOT_DIR)) {
            log_warning("graphics", "could not create screenshot dir: {}", GRAPHICS_SCREENSHOT_DIR);
            return "";
        }
    }

    // generate date prefix
    auto t_now = std::time(nullptr);
    auto tm_now = *std::gmtime(&t_now);
    auto prefix = to_string(std::put_time(&tm_now, "%Y%m%d"));

    // find next filename
    size_t id = 0;
    while (true) {
        auto filepath = fmt::format("{}\\{}_{}.png", GRAPHICS_SCREENSHOT_DIR, prefix, id);
        if (!fileutils::file_exists(filepath)) {
            return filepath;
        }

        id++;
    }
}
