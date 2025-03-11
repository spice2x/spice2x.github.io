#pragma once

#include <vector>
#include <windows.h>

#include "handler.h"

class Win7Handler : public TouchHandler {
public:

    Win7Handler();

    static bool is_available();

    virtual bool window_register(HWND hWnd) override;
    virtual bool window_unregister(HWND hWnd) override;
    virtual void handle_message(msg_handler_result &result, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
};
