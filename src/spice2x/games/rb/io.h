#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::rb {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            PoleR,
            PoleG,
            PoleB,
            EscutcheonR,
            EscutcheonG,
            EscutcheonB,
            WooferR,
            WooferG,
            WooferB,
            TitleR,
            TitleG,
            TitleB,
            TitleUpR,
            TitleUpG,
            TitleUpB
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
