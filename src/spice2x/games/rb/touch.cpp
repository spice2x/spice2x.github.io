#include "touch.h"

#include <windows.h>

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "rawinput/touch.h"
#include "touch/touch.h"
#include "touch_debug.h"
#include "touch_defs.h"
#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

static std::string WINDOW_TITLE = "REFLEC BEAT";

namespace games::rb {
    static constexpr double TOUCH_POINT_OFFSET_DIVISOR = 3.0;
    static constexpr double X_INPUT_COORDINATE_COUNT = 54.0;

    uint16_t TOUCH_SCALING = TOUCH_SCALE_DEFAULT;

    static void packet_set_bit(unsigned char *packet, int bit) {
        packet[TOUCH_PACKET_DATA_OFFSET + bit / 8] |=
            static_cast<unsigned char>(1u << (bit % 8));
    }
}

games::rb::ReflecBeatTouchDeviceHandle::ReflecBeatTouchDeviceHandle(bool log_fps) : log_fps(log_fps) {
}

void games::rb::ReflecBeatTouchDeviceHandle::grid_insert(unsigned char *data, int cursor_x, int cursor_y) {

    // read() contracts X uniformly so screen edges land within usable sensors 2..45
    int grid_x = CLAMP(
        (cursor_x * X_SENSOR_COUNT) / window_width,
        0,
        X_SENSOR_COUNT - 1);
    int grid_y = CLAMP(
        (cursor_y * Y_SENSOR_COUNT) / window_height,
        0,
        Y_SENSOR_COUNT - 1);

    // get bit positions
    int bit_x = X_SENSOR_FIRST_BIT + grid_x;
    int bit_y = Y_SENSOR_FIRST_BIT - grid_y;

    packet_set_bit(data, bit_x);
    packet_set_bit(data, bit_y);
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

        // cache the game window so read() can size against it regardless of focus
        this->game_hwnd = wnd;

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
        }

        // create touch window
        touch_create_wnd(wnd);

        // show fullscreen game window because it lost focus
        if (!GRAPHICS_WINDOWED) {
            ShowWindow(wnd, SW_SHOW);
        }

        // request automatic aspect ratio fixes
        ::rawinput::touch::ASPECT_COMPENSATION_GAME = true;

    } else {

        // fallback to dx hook
        touch_attach_dx_hook();
    }

    // show cursor on window if mouse is used
    if (!is_touch_available("ReflecBeatTouchDeviceHandle::open")) {
        ShowCursor(TRUE);
    }

    touch_debug_attach();

    return true;
}

int games::rb::ReflecBeatTouchDeviceHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

    // check buffer size
    if (nNumberOfBytesToRead < TOUCH_PACKET_SIZE) {
        return 0;
    }

    // size against the game window cached in open() (not GetForegroundWindow), so
    // touch stays correctly scaled and keeps working when the game loses focus
    HWND window = this->game_hwnd;
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
    unsigned char data[TOUCH_PACKET_SIZE] {};

    // data header
    data[0] = 0x55;
    data[2] = 0x4C;
   
    const float scale_factor = games::rb::TOUCH_SCALING / 1000.f;

    // iterate all touch points
    int offset_x = (int) (window_width / (double) X_SENSOR_COUNT / TOUCH_POINT_OFFSET_DIVISOR);
    int offset_y = (int) (window_height / (double) Y_SENSOR_COUNT / TOUCH_POINT_OFFSET_DIVISOR);
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
        x = x - (window_width * (1.f - scale_factor) / 2);
        x = x / scale_factor;
        y = y - (window_height * (1.f - scale_factor) / 2);
        y = y / scale_factor;

        if (x < 0 || window_width <= x || y < 0 || window_height <= y) {
            continue;
        }

        // point coordinates
        // keep the verified left edge fixed while adding up to one-third of a beam
        // at the right edge, preventing the finger footprint from including sensor 44
        const auto point_x = (int) (
            (x - window_width / 2.0) * X_SENSOR_COUNT / X_INPUT_COORDINATE_COUNT +
            window_width / 2.0 +
            x / (X_SENSOR_COUNT * TOUCH_POINT_OFFSET_DIVISOR));
        const auto point_y = (int) y;

        // model a finger as a 3x3 block of IR sensors around the touch point
        // this gives better accuracy (than just 1x1) since the logic below
        // can toggle anywhere from 1x1 to 2x2, and the game engine calculates
        // the center point, which means by inserting up to 8 extra blocks
        // we are emulating a sub-"pixel" resolution
        grid_insert(data, point_x, point_y);                       // center
        grid_insert(data, point_x - offset_x, point_y);            // west
        grid_insert(data, point_x - offset_x, point_y - offset_y); // northwest
        grid_insert(data, point_x - offset_x, point_y + offset_y); // southwest
        grid_insert(data, point_x + offset_x, point_y);            // east
        grid_insert(data, point_x + offset_x, point_y + offset_y); // southeast
        grid_insert(data, point_x + offset_x, point_y - offset_y); // northeast
        grid_insert(data, point_x, point_y - offset_y);            // north
        grid_insert(data, point_x, point_y + offset_y);            // south
    }

    touch_debug_publish(data, is_landscape);

    // copy data to buffer
    memcpy(lpBuffer, data, TOUCH_PACKET_SIZE);

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
    return TOUCH_PACKET_SIZE;
}

int games::rb::ReflecBeatTouchDeviceHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    return (int) nNumberOfBytesToWrite;
}

int games::rb::ReflecBeatTouchDeviceHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                                      LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::rb::ReflecBeatTouchDeviceHandle::close() {

    touch_debug_detach();

    // detach touch module
    touch_detach();

    return true;
}
