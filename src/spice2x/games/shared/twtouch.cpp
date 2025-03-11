#include "twtouch.h"

#include "util/utils.h"

namespace games::shared {

#pragma pack(push, 1)
    struct TwTouchEventReport {
        uint32_t type;
        uint8_t padding1[8];
        uint32_t status;
        uint16_t x;
        uint16_t y;
        uint8_t padding2[4];
    };
    static_assert(sizeof(TwTouchEventReport) == 0x18);
#pragma pack(pop)

    bool TwTouchDevice::open(LPCWSTR lpFileName) {
        return wcscmp(lpFileName, L"\\\\.\\TwTouchDriver") == 0;
    }

    int TwTouchDevice::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

        // ignore if buffer is too small
        if (nNumberOfBytesToRead < sizeof(TwTouchEventReport)) {
            return 0;
        }

        // get touch events once our buffer is empty
        if (this->report_buffer.empty()) {
            touch_get_events(this->report_buffer);
        }

        // check if an event is available
        if (this->report_buffer.empty()) {

            /*
             * We limit the number of continuous reads the device can do
             * since games may try to read all at once into a buffer.
             * QMA has a limit of 100 events going in at once.
             * To avoid this we have to return nothing at least once every 100 read calls.
             */
            if (this->continuous_reads >= 99) {

                // reset counter
                this->continuous_reads = 0;

            } else {

                // get touch points
                touch_get_points(this->touch_points);

                // insert fake events
                for (auto &touch_point : this->touch_points) {
                    this->report_buffer.push_back(TouchEvent {
                        .id = touch_point.id,
                        .x = touch_point.x,
                        .y = touch_point.y,
                        .type = TOUCH_MOVE,
                        .mouse = false,
                    });
                }

                // clear touch points
                this->touch_points.clear();
            }
        } else {

            // increase counter
            this->continuous_reads++;

            // pick the first event
            auto &touch_event = this->report_buffer[0];

            // build report
            TwTouchEventReport report {};

            /*
             * Known report types
             *
             * ID    Size  Desc
             * 0x01  20
             * 0x02  16
             * 0x03  20    QMA checks uint32_t at offset 12 and uint16_t at offset 16
             * 0x04  16
             * 0x05  24    Touch Event
             * 0x06  24
             * 0x08  24
             * 0x09  24    SCV has an alternate Touch Event path here
             * 0x0B  32
             * 0x0C  16
             * 0x0D  36
             * 0x0E  16
             */
            report.type = 0x05;

            // set report data
            switch (touch_event.type) {
                case TOUCH_DOWN:
                case TOUCH_MOVE:

                    // if the status is 1 it means it's pressed
                    report.status = 1;
                    report.x = CLAMP(this->offset_x + touch_event.x * this->scale_x, 0, 65535);
                    report.y = CLAMP(this->offset_y + touch_event.y * this->scale_y, 0, 65535);

                    // flip coordinates
                    if (this->flip_x) {
                        report.x = 0xFFFF - report.x;
                    }
                    if (this->flip_y) {
                        report.y = 0xFFFF - report.y;
                    }

                    break;
                case TOUCH_UP:

                    // status 3 (and 12?) is a touch up event
                    report.status = 3;
                    report.x = 0;
                    report.y = 0;

                    break;
            }

            // erase touch event
            this->report_buffer.erase(this->report_buffer.begin());

            // copy report to buffer
            memcpy(lpBuffer, &report, sizeof(report));
            return sizeof(report);
        }

        // no touch event available for read
        return 0;
    }

    int TwTouchDevice::write(LPCVOID, DWORD) {
        return 0;
    }

    int TwTouchDevice::device_io(DWORD, LPVOID, DWORD, LPVOID, DWORD) {
        return 0;
    }

    bool TwTouchDevice::close() {
        return true;
    }
}
