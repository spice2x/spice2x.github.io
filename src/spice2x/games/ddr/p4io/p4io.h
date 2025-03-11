#pragma once

#include "acioemu/acioemu.h"
#include "hooks/devicehook.h"
#include <mutex>

namespace games::ddr {
    class DDRP4IOHandle : public CustomHandle {
    private:
        acioemu::ACIOEmu *m_acio_emu;
        std::vector<uint8_t> m_read_buf;
        std::mutex m_read_buf_mutex;

        class HDXSDevice : public acioemu::ACIODeviceEmu {
        public:
            HDXSDevice();

            bool parse_msg(
                acioemu::MessageData* msg_in,
                circular_buffer<uint8_t> *response_buffer
            ) override;
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