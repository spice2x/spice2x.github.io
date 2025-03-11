#pragma once

#include "acioemu/acioemu.h"
#include "hooks/devicehook.h"

namespace games::ddr {

    class DDRP3IOHandle : public CustomHandle {
    private:
        acioemu::ACIOEmu *acio_emu;
        circular_buffer<uint8_t> read_buf = circular_buffer<uint8_t>(1024);

        class HDXSDevice : public acioemu::ACIODeviceEmu {
        public:
            HDXSDevice();

            bool parse_msg(
                acioemu::MessageData* msg_in,
                circular_buffer<uint8_t> *response_buffer
            ) override;
        };

        void write_msg(const uint8_t *data, size_t len);

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        bool close() override;
    };
}
