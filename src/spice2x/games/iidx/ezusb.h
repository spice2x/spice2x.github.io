#pragma once

#include "hooks/devicehook.h"

namespace games::iidx {

    class EZUSBHandle : public CustomHandle {
    private:

        // state
        bool init = false;
        bool init_success = false;
        char FPGA_CMD_STATE = 67;
        char FPGA_COUNTER = 0;
        int FPGA_CUR_NODE = 0;
        int SRAM_CMD = 0;
        int SRAM_PAGE = 0;
        char SRAM_DATA[62 * 12]{};

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        bool close() override;
    };
}
