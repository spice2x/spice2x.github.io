#pragma once
#include "acioemu/device.h"
#include "device.h"

namespace games::gitadora {

    class J32DSerialDevice : public GitadoraDevice {
        private:
            uint8_t coin_counter = 0;
        public:
            J32DSerialDevice();
            bool parse_msg(acioemu::MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) override;  
            struct J32D_IO_STATUS {
                uint16_t high_tom;
                uint16_t low_tom;
                uint16_t snare;
                uint16_t floor_tom;
                uint16_t left_cymbal;
                uint16_t right_cymbal;
                uint16_t hihat;
                uint16_t unk7;
                uint8_t pedals;
            };
        };
}
