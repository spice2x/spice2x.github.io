#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::mfc {
    namespace Buttons {
        enum {
            Select,
            Service,
            Test,
            Coin,
            JoystickUp,
            JoystickDown,
            JoystickStart,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
}
