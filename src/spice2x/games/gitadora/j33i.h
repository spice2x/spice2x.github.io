#pragma once

#include "acioemu/device.h"
#include "device.h"

namespace games::gitadora {

    class J33ISerialDevice : public GitadoraDevice {
        private:
            uint8_t coin_counter = 0;
        public:
            J33ISerialDevice();
            bool parse_msg(acioemu::MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) override;  
            
            // there may be more, but the game doesn't seem to care
            // TODO: add support for gyroscope, knob and wail
            struct J33I_IO_STATUS {
                uint16_t gyroscope_x;
                uint16_t gyroscope_y;
                uint16_t gyroscope_z;
                uint16_t buttons;
            };
        };
}