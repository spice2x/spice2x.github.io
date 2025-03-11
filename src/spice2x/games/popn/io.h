#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::popn {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Button1,
            Button2,
            Button3,
            Button4,
            Button5,
            Button6,
            Button7,
            Button8,
            Button9,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            Button1,
            Button2,
            Button3,
            Button4,
            Button5,
            Button6,
            Button7,
            Button8,
            Button9,
            TopLED1,
            TopLED2,
            TopLED3,
            TopLED4,
            TopLED5,
            TopLED6,
            TopLED7,
            TopLED8,
            TopLED9,
            TopLED10,
            TopLED11,
            TopLED12,
            TopLED13,
            TopLED14,
            TopLED15,
            TopLED16,
            TopLED17,
            TopLED18,
            TopLED19,
            TopLED20,
            TopLED21,
            TopLED22,
            TopLED23,
            TopLED24,
            TopLED25,
            TopLED26,
            TopLED27,
            TopLED28,
            TopLED29,
            TopLED30,
            TopLED31,
            TopLED32,
            TopLED_R,
            TopLED_G,
            TopLED_B,
            HiLamp1,
            HiLamp2,
            HiLamp3,
            HiLamp4,
            HiLamp5,
            LeftLamp1,
            LeftLamp2,
            RightLamp1,
            RightLamp2,
            WooferLED_R,
            WooferLED_G,
            WooferLED_B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Light> &get_lights();
}
