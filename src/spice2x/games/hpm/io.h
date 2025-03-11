#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::hpm {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            P1_Start,
            P1_1,
            P1_2,
            P1_3,
            P1_4,
            P2_Start,
            P2_1,
            P2_2,
            P2_3,
            P2_4,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            SPEAKER_RED,
            SPEAKER_ORANGE,
            SPEAKER_BLUE,
            P1_START,
            P1_RED_P2_GREEN,
            P1_BLUE,
            P1_YELLOW,
            P1_GREEN,
            P2_START,
            P2_RED,
            P2_BLUE,
            P2_YELLOW,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Light> &get_lights();
}
