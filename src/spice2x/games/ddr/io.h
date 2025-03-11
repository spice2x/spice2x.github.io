#pragma once

#include <vector>

#include "cfg/api.h"

namespace games::ddr {

    // all buttons in correct order
    namespace Buttons {
        enum {
            SERVICE,
            TEST,
            COIN_MECH,
            P1_START,
            P1_PANEL_UP,
            P1_PANEL_DOWN,
            P1_PANEL_LEFT,
            P1_PANEL_RIGHT,
            P1_MENU_UP,
            P1_MENU_DOWN,
            P1_MENU_LEFT,
            P1_MENU_RIGHT,
            P2_START,
            P2_PANEL_UP,
            P2_PANEL_DOWN,
            P2_PANEL_LEFT,
            P2_PANEL_RIGHT,
            P2_MENU_UP,
            P2_MENU_DOWN,
            P2_MENU_LEFT,
            P2_MENU_RIGHT,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            P1_FOOT_LEFT,
            P1_FOOT_UP,
            P1_FOOT_RIGHT,
            P1_FOOT_DOWN,

            P2_FOOT_LEFT,
            P2_FOOT_UP,
            P2_FOOT_RIGHT,
            P2_FOOT_DOWN,

            SPOT_RED,
            SPOT_BLUE,
            TOP_SPOT_RED,
            TOP_SPOT_BLUE,

            P1_HALOGEN_UPPER,
            P1_HALOGEN_LOWER,
            P2_HALOGEN_UPPER,
            P2_HALOGEN_LOWER,

            P1_BUTTON,
            P2_BUTTON,
            NEON,

            HD_P1_START,
            HD_P1_LEFT_RIGHT,
            HD_P1_UP_DOWN,

            HD_P2_START,
            HD_P2_LEFT_RIGHT,
            HD_P2_UP_DOWN,

            HD_P1_SPEAKER_F_R,
            HD_P1_SPEAKER_F_G,
            HD_P1_SPEAKER_F_B,
            HD_P1_SPEAKER_W_R,
            HD_P1_SPEAKER_W_G,
            HD_P1_SPEAKER_W_B,

            HD_P2_SPEAKER_F_R,
            HD_P2_SPEAKER_F_G,
            HD_P2_SPEAKER_F_B,
            HD_P2_SPEAKER_W_R,
            HD_P2_SPEAKER_W_G,
            HD_P2_SPEAKER_W_B,

            WHITE_SPEAKER_TOP_R,
            WHITE_SPEAKER_TOP_G,
            WHITE_SPEAKER_TOP_B,
            WHITE_SPEAKER_BOTTOM_R,
            WHITE_SPEAKER_BOTTOM_G,
            WHITE_SPEAKER_BOTTOM_B,
            WHITE_WOOFER_R,
            WHITE_WOOFER_G,
            WHITE_WOOFER_B,

            GOLD_P1_MENU_START,
            GOLD_P1_MENU_UP,
            GOLD_P1_MENU_DOWN,
            GOLD_P1_MENU_LEFT,
            GOLD_P1_MENU_RIGHT,

            GOLD_P2_MENU_START,
            GOLD_P2_MENU_UP,
            GOLD_P2_MENU_DOWN,
            GOLD_P2_MENU_LEFT,
            GOLD_P2_MENU_RIGHT,

            GOLD_TITLE_PANEL_LEFT,
            GOLD_TITLE_PANEL_CENTER,
            GOLD_TITLE_PANEL_RIGHT,

            GOLD_P1_WOOFER_CORNER,
            GOLD_P2_WOOFER_CORNER,

            GOLD_P1_CARD_UNIT_R,
            GOLD_P1_CARD_UNIT_G,
            GOLD_P1_CARD_UNIT_B,

            GOLD_P2_CARD_UNIT_R,
            GOLD_P2_CARD_UNIT_G,
            GOLD_P2_CARD_UNIT_B,

            GOLD_TOP_PANEL_AVG_R,
            GOLD_TOP_PANEL_AVG_G,
            GOLD_TOP_PANEL_AVG_B,

            GOLD_MONITOR_SIDE_LEFT_AVG_R,
            GOLD_MONITOR_SIDE_LEFT_AVG_G,
            GOLD_MONITOR_SIDE_LEFT_AVG_B,

            GOLD_MONITOR_SIDE_RIGHT_AVG_R,
            GOLD_MONITOR_SIDE_RIGHT_AVG_G,
            GOLD_MONITOR_SIDE_RIGHT_AVG_B,

            GOLD_P1_FOOT_UP_AVG_R,
            GOLD_P1_FOOT_UP_AVG_G,
            GOLD_P1_FOOT_UP_AVG_B,

            GOLD_P1_FOOT_DOWN_AVG_R,
            GOLD_P1_FOOT_DOWN_AVG_G,
            GOLD_P1_FOOT_DOWN_AVG_B,

            GOLD_P1_FOOT_LEFT_AVG_R,
            GOLD_P1_FOOT_LEFT_AVG_G,
            GOLD_P1_FOOT_LEFT_AVG_B,

            GOLD_P1_FOOT_RIGHT_AVG_R,
            GOLD_P1_FOOT_RIGHT_AVG_G,
            GOLD_P1_FOOT_RIGHT_AVG_B,

            GOLD_P2_FOOT_UP_AVG_R,
            GOLD_P2_FOOT_UP_AVG_G,
            GOLD_P2_FOOT_UP_AVG_B,

            GOLD_P2_FOOT_DOWN_AVG_R,
            GOLD_P2_FOOT_DOWN_AVG_G,
            GOLD_P2_FOOT_DOWN_AVG_B,

            GOLD_P2_FOOT_LEFT_AVG_R,
            GOLD_P2_FOOT_LEFT_AVG_G,
            GOLD_P2_FOOT_LEFT_AVG_B,

            GOLD_P2_FOOT_RIGHT_AVG_R,
            GOLD_P2_FOOT_RIGHT_AVG_G,
            GOLD_P2_FOOT_RIGHT_AVG_B,

            GOLD_P1_STAGE_UP_LEFT,
            GOLD_P1_STAGE_UP_RIGHT,
            GOLD_P1_STAGE_DOWN_LEFT,
            GOLD_P1_STAGE_DOWN_RIGHT,

            GOLD_P2_STAGE_UP_LEFT,
            GOLD_P2_STAGE_UP_RIGHT,
            GOLD_P2_STAGE_DOWN_LEFT,
            GOLD_P2_STAGE_DOWN_RIGHT,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
