#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::shogikai {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Select,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            Left,
            Right,
        };
    }

    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
