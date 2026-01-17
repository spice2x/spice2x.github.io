#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::mfg {
    namespace Buttons {
        enum {
            Test,
            Service,
            CoinMech,
            QButton,
            QButton1,
            QButton2,
            QButton3,
            JackDetect,
            MicDetect
        };
    }

    // all lights in correct order
    namespace Lights {
        typedef enum {
            TitlePanel_R,
            TitlePanel_G,
            TitlePanel_B,
            SidePanel_R,
            SidePanel_G,
            SidePanel_B,
            CardReader_R,
            CardReader_G,
            CardReader_B,
            SpecialButton,
        } mfg_lights_t;
    }

    namespace Analogs {
        enum {
            Joystick_X,
            Joystick_Y,
            Trackball_DX,
            Trackball_DY
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
