#pragma once

#include <cstdint>
#include <vector>
#include "hooks/devicehook.h"
#include "touch/touch.h"

namespace games::shared {

    class TwTouchDevice : public CustomHandle {
    private:

        // report buffer
        std::vector<TouchEvent> report_buffer;
        std::vector<TouchPoint> touch_points;
        unsigned int continuous_reads = 0;

    public:

        // settings
        float offset_x = 0.f;
        float offset_y = 0.f;
        float scale_x = 1.f;
        float scale_y = 1.f;
        bool flip_x = false;
        bool flip_y = false;

        // overrides
        bool open(LPCWSTR lpFileName) override;
        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;
        bool close() override;
    };
}
