#pragma once

#include "hooks/devicehook.h"
#include "acioemu/acioemu.h"

namespace games::ddr {

    class DDRSATEHandle : public CustomHandle {
    private:
        acioemu::ACIOEmu acio_emu;

        class SATEDevice : public acioemu::ACIODeviceEmu {
        public:
            SATEDevice();

            bool parse_msg(acioemu::MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) override;
        };

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        bool close() override;
    };
}
