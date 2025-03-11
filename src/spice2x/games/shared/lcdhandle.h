#pragma once

#include <vector>
#include <windows.h>
#include "util/logging.h"
#include "hooks/devicehook.h"

namespace games::shared {

    // current state for easy access
    extern bool LCD_ENABLED;
    extern std::string LCD_CSM;
    extern uint8_t LCD_BRI;
    extern uint8_t LCD_CON;
    extern uint8_t LCD_BL;
    extern uint8_t LCD_RED;
    extern uint8_t LCD_GREEN;
    extern uint8_t LCD_BLUE;

    class LCDHandle : public CustomHandle {
    private:
        std::vector<uint8_t> read_buffer;
        uint64_t read_time_next = 0;

        void answer(std::string s);

    public:
        bool open(LPCWSTR lpFileName) override;

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                      DWORD nOutBufferSize) override;

        bool close() override;
    };
}
