#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::silentscope {

    // all buttons in correct order
    namespace Buttons {
        enum {
            SERVICE,
            TEST,
            COIN_MECH,
            START,
            UP,
            DOWN,
            LEFT,
            RIGHT,
            GUN_PRESSED,
            SCOPE_RIGHT,
            SCOPE_LEFT,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            GUN_X,
            GUN_Y,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Analog> &get_analogs();
}
