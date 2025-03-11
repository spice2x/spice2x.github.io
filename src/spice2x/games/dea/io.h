#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::dea {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            P1Start,
            P1Left,
            P1Right,
            P2Start,
            P2Left,
            P2Right
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            TitleR,
            TitleG,
            TitleB,
            SideUpperLeftR,
            SideUpperLeftG,
            SideUpperLeftB,
            SideUpperRightR,
            SideUpperRightG,
            SideUpperRightB,
            P1Start,
            P1LRButton,
            P2Start,
            P2LRButton,
            SideLowerLeft1R,
            SideLowerLeft1G,
            SideLowerLeft1B,
            SideLowerLeft2R,
            SideLowerLeft2G,
            SideLowerLeft2B,
            SideLowerLeft3R,
            SideLowerLeft3G,
            SideLowerLeft3B,
            SideLowerRight1R,
            SideLowerRight1G,
            SideLowerRight1B,
            SideLowerRight2R,
            SideLowerRight2G,
            SideLowerRight2B,
            SideLowerRight3R,
            SideLowerRight3G,
            SideLowerRight3B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
