#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::bs {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            BottomR,
            BottomG,
            BottomB,
            LeftR,
            LeftG,
            LeftB,
            RightR,
            RightG,
            RightB,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
