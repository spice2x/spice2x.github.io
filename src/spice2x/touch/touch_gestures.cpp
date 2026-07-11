
// set version to Windows 8 to enable Windows 8 touch functions
#define _WIN32_WINNT 0x0602

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

static HINSTANCE USER32_INSTANCE = nullptr;
typedef BOOL (WINAPI *SetWindowFeedbackSetting_t)(HWND, FEEDBACK_TYPE, DWORD, UINT32, const VOID *);
static SetWindowFeedbackSetting_t pSetWindowFeedbackSetting = nullptr;

static void disable_feedback_visuals(HWND hwnd) {

    if (pSetWindowFeedbackSetting == nullptr) {
        pSetWindowFeedbackSetting = libutils::try_proc<SetWindowFeedbackSetting_t>(
            USER32_INSTANCE, "SetWindowFeedbackSetting");
    }
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

void disable_touch_gestures(HWND hwnd) {

    if (USER32_INSTANCE == nullptr) {
        USER32_INSTANCE = libutils::load_library("user32.dll");
    }
    if (USER32_INSTANCE == nullptr) {
        return;
    }

    log_info("touch_gestures",
        "disable visual feedback and gestures for touch events for HWND={}", fmt::ptr(hwnd));

    // disable the visual feedback (contact circles, tap/double-tap stars, etc.)
    disable_feedback_visuals(hwnd);

    // disable the actual gesture behaviors (press-and-hold, flicks, etc.)
    disable_gesture_behaviors(hwnd);
}
