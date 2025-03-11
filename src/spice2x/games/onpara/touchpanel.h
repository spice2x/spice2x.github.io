#pragma once

#include <cstdint>
#include <queue>

#include "hooks/devicehook.h"

namespace games::onpara {
    class TouchPanelHandle : public CustomHandle {
    private:
        union touch_panel_message {
            struct {
                char op;
                char something;
                uint16_t x;
                uint16_t y;
                uint16_t z;
            };
            char raw[8];
        };

        enum touch_panel_state {
            TOUCH_PANEL_STATE_INACTIVE,
            TOUCH_PANEL_STATE_ACKNOWLEDGE,
            TOUCH_PANEL_STATE_DIAGNOSTICS,
            TOUCH_PANEL_STATE_REPORT
        };
        
        touch_panel_state state_ = TOUCH_PANEL_STATE_INACTIVE;
        std::queue<uint8_t> output_queue_;

        void set_state(touch_panel_state state);
        void enqueue_packet(touch_panel_message const &message);

    public:
        bool open(LPCWSTR lpFileName) override;
        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize) override;
        bool close() override;
    };
}