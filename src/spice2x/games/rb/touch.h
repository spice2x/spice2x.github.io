#pragma once

#include <cstdint>
#include "hooks/devicehook.h"

namespace games::rb {

    class ReflecBeatTouchDeviceHandle : public CustomHandle {
    private:

        // state
        int window_width = 1080;
        int window_height = 1920;

        // logging
        bool log_fps = false;
        uint64_t log_time = 0;
        int log_frames = 0;

        void grid_insert(unsigned char *data, int cursor_x, int cursor_y);

    public:

        ReflecBeatTouchDeviceHandle(bool log_fps);

        bool open(LPCWSTR lpFileName) override;
        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;
        bool close() override;
    };
}
