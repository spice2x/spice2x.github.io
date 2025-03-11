#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::loveplus {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            Left,
            Right,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            Red,
            Green,
            Blue,
            Left,
            Right,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
