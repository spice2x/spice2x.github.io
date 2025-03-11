#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::jb {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Button1,
            Button2,
            Button3,
            Button4,
            Button5,
            Button6,
            Button7,
            Button8,
            Button9,
            Button10,
            Button11,
            Button12,
            Button13,
            Button14,
            Button15,
            Button16,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            PANEL_FRONT_R,
            PANEL_FRONT_G,
            PANEL_FRONT_B,
            PANEL_TITLE_R,
            PANEL_TITLE_G,
            PANEL_TITLE_B,
            PANEL_TOP_R,
            PANEL_TOP_G,
            PANEL_TOP_B,
            PANEL_LEFT_R,
            PANEL_LEFT_G,
            PANEL_LEFT_B,
            PANEL_RIGHT_R,
            PANEL_RIGHT_G,
            PANEL_RIGHT_B,
            PANEL_WOOFER_R,
            PANEL_WOOFER_G,
            PANEL_WOOFER_B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Light> &get_lights();
}
