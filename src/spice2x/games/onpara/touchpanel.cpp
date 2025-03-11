#include <thread>
#include <chrono>

#include "touchpanel.h"

#include "util/logging.h"
#include "touch/touch.h"

using namespace std::chrono_literals;

void games::onpara::TouchPanelHandle::set_state(touch_panel_state state) {
    state_ = state;
    // reset the output queue as well for good measure
    output_queue_ = {};
}

void games::onpara::TouchPanelHandle::enqueue_packet(touch_panel_message const &message) {
    // push magic
    output_queue_.push('U');

    // push message
    for (auto b : message.raw) {
        output_queue_.push(b);
    }

    // push checksum
    output_queue_.push(message.raw[0] + 
                       message.raw[1] + 
                       message.raw[2] + 
                       message.raw[3] + 
                       message.raw[4] + 
                       message.raw[5] + 
                       message.raw[6] + 
                       message.raw[7] - 1);
}

bool games::onpara::TouchPanelHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM1: baud=9600 parity=N data=8 stop=1") != 0) {
        return false;
    }
    log_info("touchpanel", "Opened COM1 (Touch Panel)");

    return true;
}

int games::onpara::TouchPanelHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    DWORD i;
    auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);

    if (state_ == TOUCH_PANEL_STATE_REPORT && output_queue_.empty()) {
        // create a new status report message
        touch_panel_message report = {
            'T', 'K', 0, 0, 0
        };

        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points);

        if (!touch_points.empty()) {
            auto &touch_point = touch_points[0];

            report.x = static_cast<uint16_t>(touch_point.x);
            report.y = 768 - static_cast<uint16_t>(touch_point.y);
            report.z = ~0;
        }

        enqueue_packet(report);

        // prevent cpu bullying
        std::this_thread::sleep_for(1ms);
    }

    // copy from output queue
    for (i = 0; i < nNumberOfBytesToRead && !output_queue_.empty(); i++) {
        buffer[i] = output_queue_.front();
        output_queue_.pop();
    }

    return i; 
}

int games::onpara::TouchPanelHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    // static messages
    static const touch_panel_message acknowledge_message = {
        'A', 0, 0, 0, 0
    };
    static const touch_panel_message diagnostics_message = {
        'D', 0, 0, 0, 0
    };

    // make sure that we received the correct amount of data
    if (nNumberOfBytesToWrite != 10) {
        log_warning("touchscreen", "invalid packet size");
        return 0;
    }

    auto op = reinterpret_cast<const char *>(lpBuffer)[1];
    switch (op) {
    case 'R': // reset
        set_state(TOUCH_PANEL_STATE_INACTIVE);
        break;
    
    case 'a': // acknowledge
        if (state_ == TOUCH_PANEL_STATE_INACTIVE) {
            set_state(TOUCH_PANEL_STATE_ACKNOWLEDGE);
            enqueue_packet(acknowledge_message);
        }
        else if (state_ == TOUCH_PANEL_STATE_DIAGNOSTICS) {
            // start reporting 
            set_state(TOUCH_PANEL_STATE_REPORT);
        }
        break;

    case 'D': // diagnositcs
        set_state(TOUCH_PANEL_STATE_DIAGNOSTICS);
        enqueue_packet(diagnostics_message);
        break;

    default:
        log_warning("touchpanel", "invalid operation: {:c}", op);
        break;
    }

    return nNumberOfBytesToWrite;
}

int games::onpara::TouchPanelHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                              LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::onpara::TouchPanelHandle::close() {
    log_info("touchpanel", "Closed COM1 (Touch Panel)");

    return true;
}