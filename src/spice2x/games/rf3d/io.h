#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::rf3d {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            View,
            Toggle2D3D,
            LeverUp,
            LeverDown,
            LeverLeft,
            LeverRight,
            WheelLeft,
            WheelRight,
            Accelerate,
            Brake,
            AutoLeverDown,
            AutoLeverUp
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            Wheel,
            Accelerate,
            Brake
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Analog> &get_analogs();
}
