#pragma once

#include <string>
#include <windows.h>

#include "touch.h"

// touch states
extern std::mutex TOUCH_POINTS_M;
extern std::mutex TOUCH_EVENTS_M;
extern std::vector<TouchPoint> TOUCH_POINTS;

enum msg_handler_action {
    /*
     * The message was unhandled by the touch handler. This is the default value set when
     * the touch handler is called.
     */
    ACTION_PASS = 0,

    /*
     * The message was handled by the touch handler, but `DefWindowProc` should be called
     * and its return value used instead of the stored one.
     */
    ACTION_RETURN_DEFAULT = 1,

    /*
     * The message was handled by the touch handler and the stored `return_value` should be
     * used as the return value.
     */
    ACTION_RETURN_STORED = 2,
};

struct msg_handler_result {
    msg_handler_action action;
    LRESULT return_value;
};

class TouchHandler {
public:

    TouchHandler(std::string name);
    virtual ~TouchHandler() = default;

    static bool is_available() {
        return false;
    }

    virtual bool window_register(HWND hWnd) {
        return true;
    }
    virtual bool window_unregister(HWND hWnd) {
        return true;
    }

    virtual void handle_message(msg_handler_result &result, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) = 0;
};

void add_touch_event(TouchEvent *te);

void update_card_button();
