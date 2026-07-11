
// set version to Windows 8 to enable Windows 8 touch functions
#define _WIN32_WINNT 0x0602

#include <propsys.h>

#include <mutex>

#include "touch_gestures.h"
#include "util/libutils.h"
#include "util/logging.h"

// tablet/pen service flags (MicrosoftTabletPenServiceProperty atom)
// these are not present in the mingw headers
#ifndef TABLET_DISABLE_PRESSANDHOLD
#define TABLET_DISABLE_PRESSANDHOLD        0x00000001
#define TABLET_DISABLE_PENTAPFEEDBACK      0x00000008
#define TABLET_DISABLE_PENBARRELFEEDBACK   0x00000010
#define TABLET_DISABLE_TOUCHUIFORCEON      0x00000100
#define TABLET_DISABLE_TOUCHUIFORCEOFF     0x00000200
#define TABLET_DISABLE_TOUCHSWITCH         0x00008000
#define TABLET_DISABLE_FLICKS              0x00010000
#define TABLET_DISABLE_SMOOTHSCROLLING     0x00080000
#define TABLET_DISABLE_FLICKFALLBACKKEYS   0x00100000
#endif

static const char TABLET_ATOM_NAME[] = "MicrosoftTabletPenServiceProperty";

// PKEY_EdgeGesture_DisableTouchWhenFullscreen format GUID
// (not defined in the mingw headers); defined inline to avoid an initguid.h
// symbol clash with touch/win8.cpp which declares the same name
// {32CE38B2-2C9A-41B1-9BC5-B3784394AA44}
static const GUID EDGEGESTURE_DISABLE_FMT =
    { 0x32CE38B2, 0x2C9A, 0x41B1, { 0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44 } };

static HINSTANCE USER32_INSTANCE = nullptr;
typedef BOOL (WINAPI *SetWindowFeedbackSetting_t)(HWND, FEEDBACK_TYPE, DWORD, UINT32, const VOID *);
static SetWindowFeedbackSetting_t pSetWindowFeedbackSetting = nullptr;

static HINSTANCE SHELL32_INSTANCE = nullptr;
typedef HRESULT (WINAPI *SHGetPropertyStoreForWindow_t)(HWND, REFIID, void **);
static SHGetPropertyStoreForWindow_t pSHGetPropertyStoreForWindow = nullptr;

static std::once_flag INIT_FLAG;

// resolve the libraries and entry points once; disable_touch_gestures may be
// called concurrently from the CreateWindowEx hooks and the touch thread
static void init_procs() {
    std::call_once(INIT_FLAG, []() {
        USER32_INSTANCE = libutils::load_library("user32.dll");
        if (USER32_INSTANCE != nullptr) {
            pSetWindowFeedbackSetting = libutils::try_proc<SetWindowFeedbackSetting_t>(
                USER32_INSTANCE, "SetWindowFeedbackSetting");
        }

        SHELL32_INSTANCE = libutils::try_library("shell32.dll");
        if (SHELL32_INSTANCE != nullptr) {
            pSHGetPropertyStoreForWindow = libutils::try_proc<SHGetPropertyStoreForWindow_t>(
                SHELL32_INSTANCE, "SHGetPropertyStoreForWindow");
        }
    });
}

static void disable_feedback_visuals(HWND hwnd) {

    if (pSetWindowFeedbackSetting == nullptr) {
        return;
    }

    BOOL enabled = FALSE;
    pSetWindowFeedbackSetting(
        hwnd,
        FEEDBACK_TOUCH_CONTACTVISUALIZATION,
        0, sizeof(enabled), &enabled);
    pSetWindowFeedbackSetting(
        hwnd,
        FEEDBACK_TOUCH_TAP,
        0, sizeof(enabled), &enabled);
    pSetWindowFeedbackSetting(
        hwnd,
        FEEDBACK_TOUCH_DOUBLETAP,
        0, sizeof(enabled), &enabled);
    pSetWindowFeedbackSetting(
        hwnd,
        FEEDBACK_TOUCH_PRESSANDHOLD,
        0, sizeof(enabled), &enabled);
    pSetWindowFeedbackSetting(
        hwnd,
        FEEDBACK_TOUCH_RIGHTTAP,
        0, sizeof(enabled), &enabled);
    pSetWindowFeedbackSetting(
        hwnd,
        FEEDBACK_GESTURE_PRESSANDTAP,
        0, sizeof(enabled), &enabled);
}

static void disable_gesture_behaviors(HWND hwnd) {

    // the tablet/pen service reads this window property to decide which
    // touch/pen gestures to suppress for the window. this covers:
    //  - press-and-hold (touch right-click / long-press ring)
    //  - pen tap/barrel feedback
    //  - flicks (edge/directional flick navigation gestures)
    //  - the touch keyboard invocation UI
    //  - smooth scrolling / flick fallback keys
    DWORD tablet_flags = TABLET_DISABLE_PRESSANDHOLD |
                         TABLET_DISABLE_PENTAPFEEDBACK |
                         TABLET_DISABLE_PENBARRELFEEDBACK |
                         TABLET_DISABLE_FLICKS |
                         TABLET_DISABLE_TOUCHUIFORCEOFF |
                         TABLET_DISABLE_TOUCHSWITCH |
                         TABLET_DISABLE_SMOOTHSCROLLING |
                         TABLET_DISABLE_FLICKFALLBACKKEYS;

    ATOM atom_id = GlobalAddAtomA(TABLET_ATOM_NAME);
    if (atom_id > 0) {
        SetPropA(hwnd, TABLET_ATOM_NAME, (HANDLE) ((ULONG_PTR) tablet_flags));
    }
}

static void disable_edge_gestures(HWND hwnd) {

    // suppress the touch edge swipes (charms/back/app bar) for the window while
    // it is fullscreen. this is normally done by the touch handler's
    // window_register(), but that is not called for every handler (e.g. the
    // rawinput handler is a no-op), so apply it here as well.
    if (pSHGetPropertyStoreForWindow == nullptr) {
        return;
    }

    IPropertyStore *ps = nullptr;
    HRESULT hr = pSHGetPropertyStoreForWindow(hwnd, IID_IPropertyStore, (void **) &ps);
    if (SUCCEEDED(hr) && ps != nullptr) {
        PROPERTYKEY key = { EDGEGESTURE_DISABLE_FMT, 2 };

        PROPVARIANT var {};
        var.vt = VT_BOOL;
        var.boolVal = VARIANT_TRUE;

        hr = ps->SetValue(key, var);
        ps->Release();

        if (FAILED(hr)) {
            log_warning("touch_gestures",
                "failed to disable edge gestures on HWND={}", fmt::ptr(hwnd));
        }
    }
}

void disable_touch_gestures(HWND hwnd) {

    init_procs();
    if (USER32_INSTANCE == nullptr) {
        return;
    }

    log_misc("touch_gestures",
        "disable visual feedback and gestures for touch events for HWND={}", fmt::ptr(hwnd));

    // disable the visual feedback (contact circles, tap/double-tap stars, etc.)
    disable_feedback_visuals(hwnd);

    // disable the actual gesture behaviors (press-and-hold, flicks, etc.)
    disable_gesture_behaviors(hwnd);

    // disable the fullscreen touch edge swipes
    disable_edge_gestures(hwnd);
}
