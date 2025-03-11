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

    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
}
