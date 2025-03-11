
// set version to Windows 8 to enable Windows 8 touch functions
#define _WIN32_WINNT 0x0602

#include "touch_indicators.h"
#include "util/libutils.h"
#include "util/logging.h"

static HINSTANCE USER32_INSTANCE = nullptr;
typedef BOOL (WINAPI *SetWindowFeedbackSetting_t)(HWND, FEEDBACK_TYPE, DWORD, UINT32, const VOID *);
static SetWindowFeedbackSetting_t pSetWindowFeedbackSetting = nullptr;

void disable_touch_indicators(HWND hwnd) {

    if (USER32_INSTANCE == nullptr) {
        USER32_INSTANCE = libutils::load_library("user32.dll");
    }
    if (USER32_INSTANCE == nullptr) {
        return;
    }

    if (pSetWindowFeedbackSetting == nullptr) {
        pSetWindowFeedbackSetting = libutils::try_proc<SetWindowFeedbackSetting_t>(
            USER32_INSTANCE, "SetWindowFeedbackSetting");
    }
    if (pSetWindowFeedbackSetting == nullptr) {
        return;
    }

    log_info("touch_indicators", "disable visual feedback for touch events for HWND={}", fmt::ptr(hwnd));

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
