#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

namespace rawinput {

    namespace SextetLights {
        enum {
            BASS_NEON = 0,

            MARQUEE_P1_UPPER,
            MARQUEE_P1_LOWER,
            MARQUEE_P2_UPPER,
            MARQUEE_P2_LOWER,

            P1_MENU,
            P2_MENU,

            P1_UP,
            P1_DOWN,
            P1_LEFT,
            P1_RIGHT,

            P2_UP,
            P2_DOWN,
            P2_LEFT,
            P2_RIGHT
        };
    }

    class SextetDevice {
    private:

        // COM port handle
        std::string device_name;
        HANDLE device;

        // number of bytes to contain the full sextet pack and a trailing LF
        static const size_t FULL_SEXTET_COUNT = 14;

        void set_all(bool state);
        void fill_packet(uint8_t *buffer, bool *light_state);

    public:

        // only 15 lights are used on SD DDR Cabinets.
        static const int LIGHT_COUNT = 15;
        static const std::string LIGHT_NAMES[];

        // state
        bool is_connected;
        bool light_state[LIGHT_COUNT];

        SextetDevice(std::string device_name);
        ~SextetDevice();

        bool connect();
        bool disconnect();

        bool push_light_state();

        void all_on();
        void all_off();
    };
}
