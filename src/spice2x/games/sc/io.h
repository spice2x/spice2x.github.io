#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::sc {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            L1,
            L2,
            LButton,
            R1,
            R2,
            RButton,
            JogLeft,
            JogRight,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            LEFT_X,
            LEFT_Y,
            RIGHT_X,
            RIGHT_Y,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            CenterRed,
            CenterGreen,
            CenterBlue,
            SideRed,
            SideGreen,
            SideBlue,
            ControllerRed,
            ControllerBlue,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
