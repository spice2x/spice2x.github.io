#pragma once

#include "acioemu/acioemu.h"
#include "hooks/devicehook.h"

namespace games::gitadora {

    class GitaDoraSerialHandle : public CustomHandle {
    private:
        acioemu::ACIOEmu acio_emu;

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;
        bool close() override;
        
    };
}
