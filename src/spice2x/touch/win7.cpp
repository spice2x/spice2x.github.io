
// enable touch functions - set version to windows 7
// mingw otherwise doesn't load touch stuff
#define _WIN32_WINNT 0x0601

#include <thread>
#include <mutex>
#include <windowsx.h>

#include "win7.h"

#include "util/libutils.h"
#include "util/logging.h"
#include "rawinput/touch.h"

// mingw issue #2205 workaround
// https://sourceforge.net/p/mingw/bugs/2205/
#undef TOUCHEVENTF_MOVE
#define TOUCHEVENTF_MOVE 0x0001
#undef TOUCHEVENTF_DOWN
#define TOUCHEVENTF_DOWN 0x0002

// mingw doesn't seem to have these
#ifndef WM_TABLET_DEFBASE
#define WM_TABLET_DEFBASE                    0x02C0
#define WM_TABLET_QUERYSYSTEMGESTURESTATUS   (WM_TABLET_DEFBASE + 12)
#define TABLET_DISABLE_PRESSANDHOLD        0x00000001
#define TABLET_DISABLE_PENTAPFEEDBACK      0x00000008
#define TABLET_DISABLE_PENBARRELFEEDBACK   0x00000010
#define TABLET_DISABLE_TOUCHUIFORCEON      0x00000100
#define TABLET_DISABLE_TOUCHUIFORCEOFF     0x00000200
#define TABLET_DISABLE_TOUCHSWITCH         0x00008000
#define TABLET_DISABLE_FLICKS              0x00010000
#define TABLET_DISABLE_SMOOTHSCROLLING     0x00080000
#define TABLET_DISABLE_FLICKFALLBACKKEYS   0x00100000
#define TABLET_ENABLE_MULTITOUCHDATA       0x01000000
#endif

// general states
static HMODULE USER32_INSTANCE = nullptr;
static const char *LOG_MODULE_NAME = "touch::win7";

/* dynamic touch functions
 * to maintain compatibility with windows XP
 */
static BOOL (WINAPI *pSetGestureConfig)(HWND, DWORD, UINT, PGESTURECONFIG, UINT);
static BOOL (WINAPI *pRegisterTouchWindow)(HWND, ULONG);
static BOOL (WINAPI *pGetTouchInputInfo)(HANDLE, UINT, PTOUCHINPUT, int);
static BOOL (WINAPI *pCloseTouchInputHandle)(HANDLE);
static BOOL (WINAPI *pUnregisterTouchWindow)(HWND hWnd);

// other
static const char ATOM_NAME[] = "MicrosoftTabletPenServiceProperty";

Win7Handler::Win7Handler() : TouchHandler("win7") {

    // check if already loaded
    static bool functions_loaded = false;
    if (functions_loaded) {
        return;
    }

    // load user32
    if (USER32_INSTANCE == nullptr) {
        USER32_INSTANCE = libutils::load_library("user32.dll");
    }

    // load touch functions
    pSetGestureConfig = libutils::try_proc<decltype(pSetGestureConfig)>(
            USER32_INSTANCE, "SetGestureConfig");
    pRegisterTouchWindow = libutils::try_proc<decltype(pRegisterTouchWindow)>(
            USER32_INSTANCE, "RegisterTouchWindow");
    pGetTouchInputInfo = libutils::try_proc<decltype(pGetTouchInputInfo)>(
            USER32_INSTANCE, "GetTouchInputInfo");
    pCloseTouchInputHandle = libutils::try_proc<decltype(pCloseTouchInputHandle)>(
            USER32_INSTANCE, "CloseTouchInputHandle");
    pUnregisterTouchWindow = libutils::try_proc<decltype(pUnregisterTouchWindow)>(
            USER32_INSTANCE, "UnregisterTouchWindow");

    functions_loaded = true;
}

bool Win7Handler::is_available() {
    bool win_touch_available = (GetSystemMetrics(94) & 0x80) == 0x80;

    if (win_touch_available) {
        log_info(LOG_MODULE_NAME, "WinTouch available");
    } else {
        log_info(LOG_MODULE_NAME, "WinTouch unavailable");
    }

    return win_touch_available;
}

bool Win7Handler::window_register(HWND hWnd) {

    bool result = true;

    // atom settings
    DWORD dwHwndTabletProperty = TABLET_DISABLE_PRESSANDHOLD |
                                 TABLET_DISABLE_PENTAPFEEDBACK |
                                 TABLET_DISABLE_PENBARRELFEEDBACK |
                                 TABLET_DISABLE_FLICKS;

    // get atom ID
    ATOM atom_id = GlobalAddAtom(ATOM_NAME);

    // disable gestures
    if (atom_id > 0) {
        SetProp(hWnd, ATOM_NAME, (HANDLE) ((unsigned long long) dwHwndTabletProperty));
    }

    // register touch window
    if ((pRegisterTouchWindow == nullptr) || (pRegisterTouchWindow(hWnd, TWF_WANTPALM) == 0)) {
        log_warning(LOG_MODULE_NAME, "could not register touch window");

        result = false;
    }

    // set gesture config
    if (pSetGestureConfig != nullptr) {
        GESTURECONFIG gc {};
        gc.dwID = 0;
        gc.dwWant = 0;
        gc.dwBlock = 1;
        pSetGestureConfig(hWnd, 0, 1, &gc, sizeof(GESTURECONFIG));
    }

    return result;
}

bool Win7Handler::window_unregister(HWND hWnd) {

    // dispose window
    if (pUnregisterTouchWindow != nullptr) {
        pUnregisterTouchWindow(hWnd);
    }

    return true;
}

void Win7Handler::handle_message(msg_handler_result &result, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    // parse touch messages
    switch (msg) {
        case WM_TOUCH: {

            // touch input fields
            UINT cInputs = LOWORD(wParam);
            auto pInputs = std::make_unique<TOUCHINPUT[]>(cInputs);

            // get touch input
            if ((pGetTouchInputInfo != nullptr) &&
                (pGetTouchInputInfo((HANDLE) lParam, cInputs, pInputs.get(), sizeof(TOUCHINPUT)) != 0)) {

                // lock touch points
                std::lock_guard<std::mutex> lock_points(TOUCH_POINTS_M);
                std::lock_guard<std::mutex> lock_events(TOUCH_EVENTS_M);

                // iterate all inputs
                static long prev_x, prev_y;
                for (UINT i = 0; i < cInputs; i++) {
                    auto &ti = pInputs[i];

                    // touch down
                    if (ti.dwFlags & TOUCHEVENTF_DOWN) {

                        // convert to window position
                        POINT point {};
                        point.x = ti.x / 100;
                        point.y = ti.y / 100;
                        ScreenToClient(hWnd, &point);

                        // create new touch point
                        TouchPoint tp {
                            .id = ti.dwID,
                            .x = point.x,
                            .y = point.y,
                            .mouse = false,
                        };
                        TOUCH_POINTS.push_back(tp);

                        // add touch down event
                        TouchEvent te {
                            .id = tp.id,
                            .x = tp.x,
                            .y = tp.y,
                            .type = TOUCH_DOWN,
                            .mouse = tp.mouse,
                        };
                        add_touch_event(&te);

                        // set prev coordinates
                        prev_x = point.x;
                        prev_y = point.y;

                        // card button
                        update_card_button();
                    }

                    // touch move
                    if ((ti.dwFlags & TOUCHEVENTF_MOVE) != 0u) {

                        // convert to window position
                        POINT point {};
                        point.x = ti.x / 100;
                        point.y = ti.y / 100;
                        ScreenToClient(hWnd, &point);

                        // check prev coordinates
                        if (point.x != prev_x || point.y != prev_y) {

                            // update point
                            for (auto &tp : TOUCH_POINTS) {
                                if (tp.id == ti.dwID) {

                                    // update values
                                    tp.x = point.x;
                                    tp.y = point.y;

                                    // add touch move event
                                    TouchEvent te {
                                        .id = tp.id,
                                        .x = tp.x,
                                        .y = tp.y,
                                        .type = TOUCH_MOVE,
                                        .mouse = tp.mouse,
                                    };
                                    add_touch_event(&te);

                                    break;
                                }
                            }
                        }

                        // set prev coordinates
                        prev_x = point.x;
                        prev_y = point.y;
                    }

                    // touch up
                    if ((ti.dwFlags & TOUCHEVENTF_UP) != 0u) {

                        // remove point
                        for (size_t x = 0; x < TOUCH_POINTS.size(); x++) {
                            auto &tp = TOUCH_POINTS[x];

                            if (tp.id == ti.dwID) {

                                // add touch up event
                                TouchEvent te {
                                    .id = tp.id,
                                    .x = tp.x,
                                    .y = tp.y,
                                    .type = TOUCH_UP,
                                    .mouse = tp.mouse,
                                };
                                add_touch_event(&te);

                                // remove from active touch points
                                TOUCH_POINTS.erase(TOUCH_POINTS.begin() + x);

                                break;
                            }
                        }
                    }
                }
            }

            // clean up
            if (pCloseTouchInputHandle != nullptr) {
                pCloseTouchInputHandle((HANDLE) lParam);
            }

            result.action = ACTION_RETURN_DEFAULT;

            break;
        }
        case WM_TABLET_QUERYSYSTEMGESTURESTATUS: {
            result.action = ACTION_RETURN_STORED;
            result.return_value = TABLET_DISABLE_PRESSANDHOLD
                   | TABLET_DISABLE_PENTAPFEEDBACK
                   | TABLET_DISABLE_PENBARRELFEEDBACK
                   | TABLET_DISABLE_TOUCHUIFORCEON
                   | TABLET_DISABLE_TOUCHUIFORCEOFF
                   | TABLET_DISABLE_TOUCHSWITCH
                   | TABLET_DISABLE_FLICKS
                   | TABLET_DISABLE_SMOOTHSCROLLING
                   | TABLET_DISABLE_FLICKFALLBACKKEYS
                   | TABLET_ENABLE_MULTITOUCHDATA;

            break;
        }
        default:
            break;
    }
}
