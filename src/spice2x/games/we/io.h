#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::we {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Start,
            Up,
            Down,
            Left,
            Right,
            ButtonA,
            ButtonB,
            ButtonC,
            ButtonD,
            ButtonE,
            ButtonF,
            PadStart,
            PadSelect,
            PadUp,
            PadDown,
            PadLeft,
            PadRight,
            PadTriangle,
            PadCross,
            PadSquare,
            PadCircle,
            PadL1,
            PadL2,
            PadL3,
            PadR1,
            PadR2,
            PadR3,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            PadStickLeftX,
            PadStickLeftY,
            PadStickRightX,
            PadStickRightY,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            LeftRed,
            LeftGreen,
            LeftBlue,
            RightRed,
            RightGreen,
            RightBlue,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
