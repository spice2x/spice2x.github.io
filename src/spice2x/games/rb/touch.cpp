#include "touch.h"

#include <windows.h>

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

static std::string WINDOW_TITLE = "REFLEC BEAT";

namespace games::rb {
    uint16_t TOUCH_SCALING = 1000;
}

games::rb::ReflecBeatTouchDeviceHandle::ReflecBeatTouchDeviceHandle(bool log_fps) {
    this->log_fps = log_fps;
}

void games::rb::ReflecBeatTouchDeviceHandle::grid_insert(unsigned char *data, int cursor_x, int cursor_y) {

    // scale to grid position - there are 48 columns and 76 rows of IR sensors.
    // for whatever reason, the last y row (#75) results in weird input few rows above; just drop it
    int grid_x = CLAMP((cursor_x * 48) / window_width, 0, 47);
    int grid_y = CLAMP((cursor_y * 76) / window_height, 0, 74);

    // get bit positions
    int bit_x = 88 + grid_x;
    int bit_y = 74 - grid_y;

    // insert bits
    data[3 + bit_x / 8] |= (char) 1 << (bit_x % 8);
    data[3 + bit_y / 8] |= (char) 1 << (bit_y % 8);
}

bool games::rb::ReflecBeatTouchDeviceHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM1") != 0) {
        return false;
    }

    // attach touch module
    HWND wnd = GetForegroundWindow();
    if (!string_begins_with(GetActiveWindowTitle(), WINDOW_TITLE)) {
        wnd = FindWindowBeginsWith(WINDOW_TITLE);
    }

    if (wnd != nullptr) {

        // reset window process to make the game not crash
        SetWindowLongPtr(wnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProc));

        // check if windowed
        if (GRAPHICS_WINDOWED) {

            // remove style borders
            LONG lStyle = GetWindowLong(wnd, GWL_STYLE);
            lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
            SetWindowLongPtr(wnd, GWL_STYLE, lStyle);

            // remove ex style borders
            LONG lExStyle = GetWindowLong(wnd, GWL_EXSTYLE);
            lExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            SetWindowLongPtr(wnd, GWL_EXSTYLE, lExStyle);

            // update window
            SetWindowPos(wnd, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

            // create touch window
            touch_create_wnd(wnd);

        } else {

            // create touch window
            touch_create_wnd(wnd);

            // show game window because it lost focus
            ShowWindow(wnd, SW_SHOW);
        }
    } else {

        // fallback to dx hook
        touch_attach_dx_hook();
    }

    // show cursor on window if mouse is used
    if (!is_touch_available()) {
        ShowCursor(TRUE);
    }

    return true;
}

int games::rb::ReflecBeatTouchDeviceHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

    // check buffer size
    if (nNumberOfBytesToRead < 20) {
        return 0;
    }

    // get window
    HWND window = GetForegroundWindow();
    if (window == nullptr) {
        return 0;
    }

    // update width and height
    RECT rect {};
    GetWindowRect(window, &rect);

    // check for landscape (KBR)
    bool is_landscape = (rect.right - rect.left) > (rect.bottom - rect.top);
    if (is_landscape) {
        window_height = rect.right - rect.left;
        window_width = rect.bottom - rect.top;
    } else {
        window_width = rect.right - rect.left;
        window_height = rect.bottom - rect.top;
    }

    // create data
    unsigned char data[20] {};

    // data header
    data[0] = 0x55;
    data[2] = 0x4C;
   
    const auto SCALE_FACTOR = games::rb::TOUCH_SCALING / 1000.f;

    // iterate all touch points
    auto offset_x = (int) (window_width / 48.0 / 3.0);
    auto offset_y = (int) (window_height / 76.0 / 3.0);
    std::vector<TouchPoint> touch_points;
    touch_get_points(touch_points);
    for (auto &point : touch_points) {

        // get touch point coordinates
        auto x = point.x;
        auto y = point.y;

        // rotate touch points for KBR as it runs in landscape
        if (is_landscape) {
            x = point.y;
            y = window_height - point.x;
        }

        // apply scaling
        x = x - (window_width * (1.f - SCALE_FACTOR) / 2);
        x = x / SCALE_FACTOR;
        y = y - (window_height * (1.f - SCALE_FACTOR) / 2);
        y = y / SCALE_FACTOR;

        if (x < 0 || window_width <= x || y < 0 || window_height <= y) {
            continue;
        }

        // point scaling
        const auto point_x = (int) ((x - window_width / 2.0) * 48.0 / 54.0 + window_width / 2.0);
        const auto point_y = (int) (y - window_height / 76);

        // insert 9 times with offset to double the precision
        grid_insert(data, point_x, point_y);
        grid_insert(data, point_x - offset_x, point_y);
        grid_insert(data, point_x - offset_x, point_y - offset_y);
        grid_insert(data, point_x - offset_x, point_y + offset_y);
        grid_insert(data, point_x + offset_x, point_y);
        grid_insert(data, point_x + offset_x, point_y + offset_y);
        grid_insert(data, point_x + offset_x, point_y - offset_y);
        grid_insert(data, point_x, point_y - offset_y);
        grid_insert(data, point_x, point_y + offset_y);
    }

    // copy data to buffer
    memcpy(lpBuffer, data, 20);

    // update frame logging
    if (log_fps) {
        log_frames++;
        if (log_time < get_system_seconds()) {
            if (log_time > 0) {
                log_info("reflecbeat", "polling at {} touch frames per second", log_frames);
            }
            log_frames = 0;
            log_time = get_system_seconds();
        }
    }

    // return amount of bytes written
    return 20;
}

int games::rb::ReflecBeatTouchDeviceHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    return (int) nNumberOfBytesToWrite;
}

int games::rb::ReflecBeatTouchDeviceHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                                      LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::rb::ReflecBeatTouchDeviceHandle::close() {

    // detach touch module
    touch_detach();

    return true;
}
