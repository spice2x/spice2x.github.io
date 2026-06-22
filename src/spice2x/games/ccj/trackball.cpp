#include "trackball.h"

#include <chrono>
#include <thread>
#include "util/detour.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "games/io.h"
#include "util/precise_timer.h"
#include "util/utils.h"
#include "io.h"

namespace games::ccj {
    bool MOUSE_TRACKBALL = false;
    bool MOUSE_TRACKBALL_USE_TOGGLE = false;
    uint8_t TRACKBALL_SENSITIVITY = 10;

    static HANDLE fakeHandle = (HANDLE)0xDEADBEEFull;
    static HWND hWnd = nullptr;
    static WNDPROC wndProc = nullptr;
    static std::thread *tbThread = nullptr;
    static bool tbThreadRunning = false;

    static const wchar_t *fakeDeviceName = L"VID_1241&PID_1111";
    static const wchar_t *windowName = L"ChaseProject";

    static decltype(GetRawInputDeviceList) *GetRawInputDeviceList_orig = nullptr;
    static decltype(GetRawInputDeviceInfoW) *GetRawInputDeviceInfoW_orig = nullptr;
    static decltype(SetWindowLongPtrW) *SetWindowLongPtrW_orig = nullptr;
    static decltype(GetRawInputData) *GetRawInputData_orig = nullptr;
    static decltype(RegisterRawInputDevices) *RegisterRawInputDevices_orig = nullptr;

    static UINT WINAPI GetRawInputDeviceList_hook(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices,
                                                  UINT cbSize) {
        auto result = GetRawInputDeviceList_orig(pRawInputDeviceList, puiNumDevices, cbSize);
        if (result == 0xFFFFFFFF) {
            return result;
        }

        if (pRawInputDeviceList == NULL) {
            (*puiNumDevices)++;
        } else if (result < *puiNumDevices) {
            pRawInputDeviceList[result] = { fakeHandle, 0 };
            result++;
        }

        return result;
    }

    static UINT WINAPI GetRawInputDeviceInfoW_hook(HANDLE hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) {
        if (hDevice != fakeHandle || uiCommand != RIDI_DEVICENAME) {
            return GetRawInputDeviceInfoW_orig(hDevice, uiCommand, pData, pcbSize);
        }

        const auto requiredLen = (wcslen(fakeDeviceName) + 1) * sizeof(wchar_t);

        if (*pcbSize < requiredLen) {
            *pcbSize = requiredLen;
            return 0xFFFFFFFF;
        }

        if (pData == NULL) {
            *pcbSize = requiredLen;
            return 0;
        }

        wcscpy((wchar_t*)pData, fakeDeviceName);
        return requiredLen;
    }

    static LONG_PTR WINAPI SetWindowLongPtrW_hook(HWND _hWnd, int nIndex, LONG_PTR dwNewLong) {
        wchar_t buffer[256];
        if (nIndex != GWLP_WNDPROC || GetWindowTextW(_hWnd, buffer, 256) == 0 || !wcswcs(buffer, windowName)) {
            return SetWindowLongPtrW_orig(_hWnd, nIndex, dwNewLong);
        }

        hWnd = _hWnd;
        wndProc = (WNDPROC)dwNewLong;
        return SetWindowLongPtrW_orig(_hWnd, nIndex, dwNewLong);
    }

    // compute the cursor wrap region in client coordinates. The cursor is confined to the
    // monitor, so if the window's client area extends past a screen edge (window larger than /
    // offset off the monitor) the cursor can never reach that far client edge. Clamp the region
    // to the on-screen portion of the client area so the right/bottom edges wrap as reliably as
    // the left/top edges.
    static RECT trackball_wrap_bounds(HWND wnd, const RECT &client) {
        RECT bounds = client;

        POINT origin = { 0, 0 };
        ClientToScreen(wnd, &origin);
        RECT clientScreen = {
            origin.x,
            origin.y,
            origin.x + client.right,
            origin.y + client.bottom
        };

        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        RECT usable;
        if (GetMonitorInfo(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST), &mi)
            && IntersectRect(&usable, &clientScreen, &mi.rcMonitor)) {
            bounds.left = usable.left - origin.x;
            bounds.top = usable.top - origin.y;
            bounds.right = usable.right - origin.x;
            bounds.bottom = usable.bottom - origin.y;
        }

        return bounds;
    }

    // drive the trackball from the physical mouse cursor, wrapping it at the window edges so it
    // can spin indefinitely. gated by the secondary-mouse button (hold or debounced toggle).
    static void trackball_mouse_input(RAWMOUSE &rawMouse) {
        static bool active = false;
        static bool lastState = false;
        static std::chrono::steady_clock::time_point lastModified = std::chrono::steady_clock::now();
        static const std::chrono::milliseconds debounceDuration(100);

        const auto currentTime = std::chrono::steady_clock::now();
        const bool pressed = get_async_secondary_mouse();
        const bool focused = GetForegroundWindow() == hWnd;

        if (focused && MOUSE_TRACKBALL_USE_TOGGLE && pressed && (currentTime - lastModified > debounceDuration)) {
            active = !active;
            lastModified = currentTime;
        }

        const bool engaged = focused
            && ((MOUSE_TRACKBALL_USE_TOGGLE && active) || (!MOUSE_TRACKBALL_USE_TOGGLE && pressed));
        if (!engaged) {
            if (lastState && !active) {
                lastState = false;
            }
            return;
        }

        POINT cursor;
        RECT client;
        GetClientRect(hWnd, &client);
        GetCursorPos(&cursor);
        ScreenToClient(hWnd, &cursor);

        const RECT bounds = trackball_wrap_bounds(hWnd, client);

        static int lastX = cursor.x;
        static int lastY = cursor.y;
        if (!lastState) {
            lastX = cursor.x;
            lastY = cursor.y;
            lastState = true;
        }

        rawMouse.usFlags = MOUSE_MOVE_RELATIVE;
        rawMouse.lLastX = (int)((float)(cursor.x - lastX) * (float)TRACKBALL_SENSITIVITY / 20.0f);
        rawMouse.lLastY = (int)((float)(lastY - cursor.y) * (float)TRACKBALL_SENSITIVITY / 20.0f);

        // wrap the cursor to the opposite edge once it reaches a boundary, so the trackball
        // can keep spinning past the screen edge.
        bool updateCursor = false;
        auto wrap = [&updateCursor](LONG value, LONG lo, LONG hi) -> LONG {
            if (value <= lo) {
                updateCursor = true;
                return hi - 5;
            }
            if (value >= hi - 1) {
                updateCursor = true;
                return lo + 5;
            }
            return value;
        };

        cursor.x = wrap(cursor.x, bounds.left, bounds.right);
        cursor.y = wrap(cursor.y, bounds.top, bounds.bottom);

        lastX = cursor.x;
        lastY = cursor.y;

        if (updateCursor) {
            ClientToScreen(hWnd, &cursor);
            SetCursorPos(cursor.x, cursor.y);
        }
    }

    // drive the trackball from the configured analog axes / direction buttons.
    static void trackball_mapped_input(RAWMOUSE &rawMouse) {
        rawMouse.usFlags = MOUSE_MOVE_RELATIVE;

        auto &analogs = get_analogs();
        if (analogs[Analogs::Trackball_DX].isSet() || analogs[Analogs::Trackball_DY].isSet()) {
            float x = GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::Trackball_DX]) * 2.0f - 1.0f;
            float y = GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::Trackball_DY]) * 2.0f - 1.0f;
            rawMouse.lLastX = (long) (x * (float) TRACKBALL_SENSITIVITY);
            rawMouse.lLastY = (long) (-y * (float) TRACKBALL_SENSITIVITY);
        }

        auto &buttons = get_buttons();
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Trackball_Up])) {
            rawMouse.lLastY = TRACKBALL_SENSITIVITY;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Trackball_Down])) {
            rawMouse.lLastY = -TRACKBALL_SENSITIVITY;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Trackball_Left])) {
            rawMouse.lLastX = -TRACKBALL_SENSITIVITY;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Trackball_Right])) {
            rawMouse.lLastX = TRACKBALL_SENSITIVITY;
        }
    }

    static UINT WINAPI GetRawInputData_hook(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader) {
        if (hRawInput != fakeHandle) {
            return GetRawInputData_orig(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        }

        if (pData == NULL) {
            if (uiCommand == RID_HEADER) {
                *pcbSize = sizeof(RAWINPUTHEADER);
            } else {
                *pcbSize = sizeof(RAWINPUT);
            }
            return 0;
        }

        const RAWINPUTHEADER header = { RIM_TYPEMOUSE, sizeof(RAWINPUT), fakeHandle, 0 };

        if (uiCommand == RID_HEADER) {
            if (*pcbSize < sizeof(RAWINPUTHEADER)) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return 0xFFFFFFFF;
            }

            *((RAWINPUTHEADER*)pData) = header;
            return sizeof(RAWINPUTHEADER);
        } else if (uiCommand == RID_INPUT) {
            if (*pcbSize < sizeof(RAWINPUT)) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return 0xFFFFFFFF;
            }

            RAWMOUSE rawMouse {};

            if (MOUSE_TRACKBALL) {
                trackball_mouse_input(rawMouse);
            } else {
                trackball_mapped_input(rawMouse);
            }

            *((RAWINPUT*)pData) = { header, { rawMouse } };
            return sizeof(RAWINPUT);
        }

        return 0xFFFFFFFF;
    }

    static BOOL WINAPI RegisterRawInputDevices_hook(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize) {

        // if the caller is spice itself, then pass through.
        if (pRawInputDevices &&
            (uiNumDevices > 0) &&
            (pRawInputDevices[0].hwndTarget == RI_MGR->input_hwnd)) {

            return RegisterRawInputDevices_orig(pRawInputDevices, uiNumDevices, cbSize);
        }

        // otherwise, it must be the game; prevent the game from registering for raw input
        // and hijacking WM_INPUT messages; we need that for rawinput to work.
        // even if we drop this, trackball emulation and mouse-as-touch input still work
        return TRUE;
    }


    void trackball_hook_init() {
        // avoid double init
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;

        // announce
        log_info("trackball", "init");

        // user32
        const auto user32Dll = "user32.dll";
        detour::trampoline_try(user32Dll, "GetRawInputDeviceList",
                               GetRawInputDeviceList_hook, &GetRawInputDeviceList_orig);
        detour::trampoline_try(user32Dll, "GetRawInputDeviceInfoW",
                               GetRawInputDeviceInfoW_hook, &GetRawInputDeviceInfoW_orig);
        detour::trampoline_try(user32Dll, "SetWindowLongPtrW",
                               SetWindowLongPtrW_hook, &SetWindowLongPtrW_orig);
        detour::trampoline_try(user32Dll, "GetRawInputData",
                               GetRawInputData_hook, &GetRawInputData_orig);
        detour::trampoline_try(user32Dll, "RegisterRawInputDevices",
                               RegisterRawInputDevices_hook, &RegisterRawInputDevices_orig);
    }

    void trackball_thread_start() {
        tbThreadRunning = true;

        log_info("trackball", "thread start, use mouse: {}, toggle: {}", MOUSE_TRACKBALL, MOUSE_TRACKBALL_USE_TOGGLE);

        tbThread = new std::thread([&] {
            timeutils::PreciseSleepTimer timer;
            while (tbThreadRunning) {
                if (hWnd && wndProc) {
                    wndProc(hWnd, WM_INPUT, RIM_INPUT, (LPARAM)fakeHandle);
                }

                if (!tbThreadRunning) {
                    break;
                }

                timer.sleep(10);
            }
        });
    }

    void trackball_thread_stop() {
        tbThreadRunning = false;
        if (tbThread) {
            tbThread->join();
        }

        log_info("trackball", "thread stop");

        delete tbThread;
    }
}
