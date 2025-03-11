#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::drs {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            P1_Start,
            P1_Up,
            P1_Down,
            P1_Left,
            P1_Right,
            P2_Start,
            P2_Up,
            P2_Down,
            P2_Left,
            P2_Right
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            P1_START,
            P1_MENU_UP,
            P1_MENU_DOWN,
            P1_MENU_LEFT,
            P1_MENU_RIGHT,
            P2_START,
            P2_MENU_UP,
            P2_MENU_DOWN,
            P2_MENU_LEFT,
            P2_MENU_RIGHT,
            CARD_READER_R,
            CARD_READER_G,
            CARD_READER_B,
            TITLE_PANEL_R,
            TITLE_PANEL_G,
            TITLE_PANEL_B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Light> &get_lights();
}
