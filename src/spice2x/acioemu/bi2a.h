#pragma once

#include <ctime>
#include <thread>
#include <mutex>
#include <cstring>
#include "device.h"
#include "hooks/sleephook.h"

namespace acioemu {

#pragma pack(push, 1)
    struct bio2_bi2a_state_in {
        uint8_t pad0[3];
        uint8_t panel[4];
        uint8_t deck_switch[14];
        uint8_t pad21[2];
        uint8_t led_ticker[9];
        uint8_t spot_light_1[4];
        uint8_t neon_light;
        uint8_t spot_light_2[4];
        uint8_t pad41[7];
    };

    struct bio2_bi2a_status {
        uint8_t slider_1;
        uint8_t system;
        uint8_t slider_2;
        uint8_t pad3;
        uint8_t slider_3;
        uint8_t pad5;
        uint8_t slider_4;
        uint8_t slider_5;
        uint8_t pad8;
        uint8_t panel;
        uint8_t pad10[6];
        uint8_t tt_p1;
        uint8_t tt_p2;
        uint8_t p1_s1;
        uint8_t pad20;
        uint8_t p1_s2;
        uint8_t pad22;
        uint8_t p1_s3;
        uint8_t pad24;
        uint8_t p1_s4;
        uint8_t pad26;
        uint8_t p1_s5;
        uint8_t pad28;
        uint8_t p1_s6;
        uint8_t pad30;
        uint8_t p1_s7;
        uint8_t pad32;
        uint8_t p2_s1;
        uint8_t pad34;
        uint8_t p2_s2;
        uint8_t pad36;
        uint8_t p2_s3;
        uint8_t pad38;
        uint8_t p2_s4;
        uint8_t pad40;
    };
#pragma pack(pop)

    class BI2A : public ACIODeviceEmu {
    private:
        uint8_t coin_counter = 0;

    public:
        explicit BI2A(bool type_new, bool flip_order, bool keypad_thread, uint8_t node_count);
        ~BI2A() override;

        bool parse_msg(unsigned int node_offset,
                       MessageData *msg_in,
                       circular_buffer<uint8_t> *response_buffer) override;

        void update_card(int unit);
        void update_keypad(int unit, bool update_edge);
        void update_status(int unit);
    };
}
