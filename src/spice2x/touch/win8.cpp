
// set version to Windows 8 to enable Windows 8 touch functions
#define _WIN32_WINNT 0x0602

#include <thread>
#include <mutex>
#include <initguid.h>
#include <windowsx.h>
#include <propsys.h>

#include "win8.h"

#include "util/libutils.h"
#include "util/logging.h"
#include "rawinput/touch.h"

// mingw does not seem to have this either
DEFINE_GUID(PKEY_EdgeGesture_DisableTouchWhenFullscreen_Format, 0x32CE38B2, 0x2C9A, 0x41B1, 0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44);

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
static HINSTANCE USER32_INSTANCE = nullptr;
static HINSTANCE SHELL32_INSTANCE = nullptr;
const char *LOG_MODULE_NAME = "touch::win8";

/*
 * dynamic touch functions
 * to maintain compatibility with windows XP
 */
typedef BOOL (WINAPI *GetPointerFrameInfoHistory_t)(UINT32, UINT32 *, UINT32 *, POINTER_INFO *);
typedef BOOL (WINAPI *SkipPointerFrameMessages_t)(UINT32);
typedef HRESULT (WINAPI *SHGetPropertyStoreForWindow_t)(HWND, REFIID, void **);
static GetPointerFrameInfoHistory_t pGetPointerFrameInfoHistory = nullptr;
static SkipPointerFrameMessages_t pSkipPointerFrameMessages = nullptr;
static SHGetPropertyStoreForWindow_t pSHGetPropertyStoreForWindow = nullptr;

// other
static const char ATOM_NAME[] = "MicrosoftTabletPenServiceProperty";

static void load_functions() {

    // check if already loaded
    static bool functions_loaded = false;
    if (functions_loaded) {
        return;
    }

    // load libraries
    if (USER32_INSTANCE == nullptr) {
        USER32_INSTANCE = libutils::load_library("user32.dll");
    }
    if (SHELL32_INSTANCE == nullptr) {
        SHELL32_INSTANCE = libutils::try_library("shell32.dll");
    }

    // load touch functions
    pGetPointerFrameInfoHistory = libutils::try_proc<GetPointerFrameInfoHistory_t>(
            USER32_INSTANCE, "GetPointerFrameInfoHistory");
    pSkipPointerFrameMessages = libutils::try_proc<SkipPointerFrameMessages_t>(
            USER32_INSTANCE, "SkipPointerFrameMessages");

    if (SHELL32_INSTANCE != nullptr) {
        pSHGetPropertyStoreForWindow = libutils::try_proc<SHGetPropertyStoreForWindow_t>(
                SHELL32_INSTANCE, "SHGetPropertyStoreForWindow");
    }

    functions_loaded = true;
}

Win8Handler::Win8Handler() : TouchHandler("win8") {
    load_functions();
}

bool Win8Handler::is_available() {
    bool have_digitizers = (GetSystemMetrics(94) & 0x80) == 0x80;

    load_functions();

    if (pGetPointerFrameInfoHistory != nullptr) {
        if (have_digitizers) {
            log_info(LOG_MODULE_NAME, "Pointer API available");
        } else {
            log_info(LOG_MODULE_NAME, "Pointer API available, but no touch screens active");
        }
    } else {
        log_info(LOG_MODULE_NAME, "Pointer API unavailable");
    }

    return have_digitizers && pGetPointerFrameInfoHistory != nullptr;
}

bool Win8Handler::window_register(HWND hWnd) {

    // atom settings
    DWORD dwHwndTabletProperty = TABLET_DISABLE_PRESSANDHOLD |
                                 TABLET_DISABLE_PENTAPFEEDBACK |
                                 TABLET_DISABLE_PENBARRELFEEDBACK |
                                 TABLET_DISABLE_FLICKS;

    // get atom ID
    ATOM atomID = GlobalAddAtom(ATOM_NAME);

    // disable gestures
    if (atomID > 0) {
        SetProp(hWnd, ATOM_NAME, (HANDLE) ((unsigned long long) dwHwndTabletProperty));
    }

    IPropertyStore *ps = nullptr;
    HRESULT hr = pSHGetPropertyStoreForWindow(hWnd, IID_IPropertyStore, (void **) &ps);

    if (SUCCEEDED(hr) && ps != nullptr) {
        PROPERTYKEY PKEY_EdgeGesture_DisableTouchWhenFullscreen = {
            PKEY_EdgeGesture_DisableTouchWhenFullscreen_Format,
            2,
        };

        PROPVARIANT var {};
        var.vt = VT_BOOL;
        var.boolVal = VARIANT_TRUE;

        hr = ps->SetValue(PKEY_EdgeGesture_DisableTouchWhenFullscreen, var);

        ps->Release();

        if (FAILED(hr)) {
            log_warning(LOG_MODULE_NAME, "Failed to disable edge gestures on window");
        }
    }

    return true;
}

bool Win8Handler::window_unregister(HWND hWnd) {
    return true;
}

void Win8Handler::handle_message(msg_handler_result &result, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    // parse touch messages
    switch (msg) {
        case WM_POINTERACTIVATE: {
            result.action = ACTION_RETURN_STORED;
            result.return_value = PA_ACTIVATE;

            break;
        }
        case WM_NCPOINTERUPDATE:
        case WM_NCPOINTERDOWN:
        case WM_NCPOINTERUP:
        case WM_POINTERUPDATE:
        case WM_POINTERDOWN:
        case WM_POINTERUP: {

            UINT entries_count = 0, pointer_count = 0;

            if (pGetPointerFrameInfoHistory == nullptr) {
                break;
            }

            if (!pGetPointerFrameInfoHistory(GET_POINTERID_WPARAM(wParam), &entries_count, &pointer_count, NULL)) {
                break;
            }

            if (entries_count * pointer_count == 0) {
                break;
            }

            auto pointer_info = std::make_unique<POINTER_INFO[]>(entries_count * pointer_count);

            if (!pGetPointerFrameInfoHistory(GET_POINTERID_WPARAM(wParam), &entries_count, &pointer_count, pointer_info.get())) {
                break;
            }

            // lock touch points
            std::lock_guard<std::mutex> lock_points(TOUCH_POINTS_M);
            std::lock_guard<std::mutex> lock_events(TOUCH_EVENTS_M);

            // iterate all inputs
            static long prev_x, prev_y;
            for (size_t i = 0; i < entries_count * pointer_count; i++) {
                auto &pi = pointer_info[i];

                if (pi.pointerFlags & POINTER_FLAG_DOWN) {

                    // convert to window position
                    POINT point = pi.ptPixelLocation;
                    ScreenToClient(hWnd, &point);

                    // create new touch point
                    TouchPoint tp {
                        .id = pi.pointerId,
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

                }

                if ((pi.pointerFlags & POINTER_FLAG_UPDATE) != 0) {

                    // convert to window position
                    POINT point = pi.ptPixelLocation;
                    ScreenToClient(hWnd, &point);

                    // check prev coordinates
                    if (point.x != prev_x || point.y != prev_y) {

                        // update point
                        for (auto &tp : TOUCH_POINTS) {
                            if (tp.id == pi.pointerId) {

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

                if ((pi.pointerFlags & POINTER_FLAG_UP) != 0) {

                    // remove point
                    for (size_t x = 0; x < TOUCH_POINTS.size(); x++) {
                        auto &tp = TOUCH_POINTS[x];

                        if (tp.id == pi.pointerId) {

                            // add touch up event
                            TouchEvent te {
                                .id = pi.pointerId,
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

            if (pSkipPointerFrameMessages != nullptr) {
                pSkipPointerFrameMessages(GET_POINTERID_WPARAM(wParam));
            }

            // card button
            update_card_button();

            result.action = ACTION_RETURN_STORED;
            result.return_value = 0;

            break;
        }
        default:
            break;
    }
}
