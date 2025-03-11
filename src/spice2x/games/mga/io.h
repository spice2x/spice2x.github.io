#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::mga {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Start,
            Top,
            FrontTop,
            FrontBottom,
            SideLeft,
            SideRight,
            SideLever,
            TriggerButton,
            SwitchButton,
            JoyForwards,
            JoyBackwards,
            JoyLeft,
            JoyRight,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            JoyX,
            JoyY,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            Start,
            LeftR,
            LeftG,
            LeftB,
            RightR,
            RightG,
            RightB,
            GunR,
            GunG,
            GunB,
            GunVibration,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
