#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::scotto {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Start,
            Up,
            Down,
            Cup1,
            Cup2,
            FirstPad,
            PadA,
            PadB,
            PadC,
            PadD,
            PadE,
            PadF,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            FIRST_PAD_R,
            FIRST_PAD_G,
            FIRST_PAD_B,
            PAD_A_R,
            PAD_A_G,
            PAD_A_B,
            PAD_B_R,
            PAD_B_G,
            PAD_B_B,
            PAD_C_R,
            PAD_C_G,
            PAD_C_B,
            PAD_D_R,
            PAD_D_G,
            PAD_D_B,
            PAD_E_R,
            PAD_E_G,
            PAD_E_B,
            PAD_F_R,
            PAD_F_G,
            PAD_F_B,
            CUP_R,
            CUP_G,
            CUP_B,
            BUTTON,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
