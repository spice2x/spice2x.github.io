#pragma once

#include "acioemu/acioemu.h"
#include "hooks/devicehook.h"

namespace acioemu {

    class ACIOHandle : public CustomHandle {
    private:
        LPCWSTR com_port;

        uint8_t icca_node_count;

        acioemu::ACIOEmu acio_emu;

    public:
        ACIOHandle(LPCWSTR lpCOMPort, uint8_t iccaNodeCount = 2);

        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        size_t bytes_available() override;

        bool close() override;
    };
}
