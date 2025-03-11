#pragma once

#include "hooks/devicehook.h"

namespace games::ddr {

    class DDRUSBMEMHandle : public CustomHandle {
    private:
        char response_data[256]{};
        size_t response_data_size = 0;

        void respond(const char *data);

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        bool close() override;
    };
}
