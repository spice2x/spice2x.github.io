#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::ftt {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            Pad1,
            Pad2,
            Pad3,
            Pad4,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            Pad1,
            Pad2,
            Pad3,
            Pad4,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            Pad1_R,
            Pad1_G,
            Pad1_B,
            Pad2_R,
            Pad2_G,
            Pad2_B,
            Pad3_R,
            Pad3_G,
            Pad3_B,
            Pad4_R,
            Pad4_G,
            Pad4_B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
