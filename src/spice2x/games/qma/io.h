#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::qma {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            Select,
            CoinMech,
            Select1,
            Select2,
            Left,
            Right,
            OK,
            TouchKey1,
            TouchKey2,
            TouchKey3,
            TouchKey4,
            TouchKey5,
            TouchKey6,
            TouchKey7,
            TouchKey8,
            TouchKey9,
            TouchKey0,
            TouchKeyDash,
            TouchKeyQ,
            TouchKeyW,
            TouchKeyE,
            TouchKeyR,
            TouchKeyT,
            TouchKeyY,
            TouchKeyU,
            TouchKeyI,
            TouchKeyO,
            TouchKeyP,
            TouchKeyA,
            TouchKeyS,
            TouchKeyD,
            TouchKeyF,
            TouchKeyG,
            TouchKeyH,
            TouchKeyJ,
            TouchKeyK,
            TouchKeyL,
            TouchKeyZ,
            TouchKeyX,
            TouchKeyC,
            TouchKeyV,
            TouchKeyB,
            TouchKeyN,
            TouchKeyM,
            TouchKeyBackspace,
            TouchKeyEnter
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            LampRed,
            LampGreen,
            LampBlue,
            ButtonLeft,
            ButtonRight,
            ButtonOK
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
