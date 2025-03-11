#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::otoca {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            ButtonLeft,
            ButtonRight,
            LeverUp,
            LeverDown,
            LeverLeft,
            LeverRight,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            LeftButton,
            RightButton,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
