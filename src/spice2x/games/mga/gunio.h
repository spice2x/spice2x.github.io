#pragma once

#include "hooks/devicehook.h"

namespace games::mga {

    // Team FrontLine GUN IO
    class SpiceGearGunHandle : public CustomHandle {
    private:

        unsigned char version = 62;
        unsigned char command = 0x00;
        unsigned short p1_x = 0;
        unsigned short p1_y = 0;
        int frequency = 1;

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        bool close() override;
    };
}
