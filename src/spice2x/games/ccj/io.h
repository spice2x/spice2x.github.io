#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::ccj {
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Joystick_Up,
            Joystick_Down,
            Joystick_Left,
            Joystick_Right,
            Button_Dash,
            Button_Action,
            Button_Jump,
            Button_Slide,
            Button_Special,
            Headphones,
            Trackball_Up,
            Trackball_Down,
            Trackball_Left,
            Trackball_Right
        };
    }

    namespace Analogs {
        enum {
            Joystick_X,
            Joystick_Y,
            Trackball_DX,
            Trackball_DY
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
        } ccj_lights_t;
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
