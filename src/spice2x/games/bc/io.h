#pragma once

#include <vector>

#include "cfg/api.h"

namespace games::bc {
    namespace Buttons {
        enum {
            Service,
            Test,
            Up,
            Down,
            Left,
            Right,
            JoystickButton,
            Trigger1,
            Trigger2,
            Button1,
            Button2,
            Button3,
            Button4,
        };
    }

    namespace Analogs {
        enum {
            StickX,
            StickY,
        };
    }

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
            Joystick_R,
            Joystick_G,
            Joystick_B,
            Button,
            IrLed,
        } bc_lights_t;
    }

    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
