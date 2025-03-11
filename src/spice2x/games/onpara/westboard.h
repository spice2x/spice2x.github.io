#pragma once

#include <array>

#include "hooks/devicehook.h"
#include "acioemu/acioemu.h"

namespace games::onpara {
    class WestBoardHandle : public CustomHandle  {
    private:
        acioemu::ACIOEmu acio_emu_;

        class WestBoardDevice : public acioemu::ACIODeviceEmu {
        private:
            enum security_plug {
                SECURITY_PLUG_LICENSE,
                SECURITY_PLUG_ACCOUNT,
            };

            enum acio_westboard_cmd {
                ACIO_WESTBOARD_CMD_SET_LIGHTS         = 0x0112,
                ACIO_WESTBOARD_CMD_EXECUTE            = 0x0130,
                ACIO_WESTBOARD_CMD_POLL_BUTTONS_0     = 0x0111,
                ACIO_WESTBOARD_CMD_POLL_BUTTONS_1     = 0x0113,
                ACIO_WESTBOARD_CMD_ENABLE_CARD_READER = 0x0131,
                ACIO_WESTBOARD_CMD_POLL_CARD_READER   = 0x0134,
                ACIO_WESTBOARD_CMD_LOAD_SECPLUG_ID    = 0x0140,
                ACIO_WESTBOARD_CMD_LOAD_SECPLUG_MODEL = 0x0141,
                ACIO_WESTBOARD_CMD_QUERY_SECPLUG      = 0x0148
            };

            std::array<uint8_t, 6> secplug_model_;
            acioemu::MessageData response_;

        public:
            WestBoardDevice();
            bool parse_msg(acioemu::MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) override;
        };

    public:
        WestBoardHandle();

        bool open(LPCWSTR lpFileName) override;
        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize) override;
        bool close() override;
    };
}
