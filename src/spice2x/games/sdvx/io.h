#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::sdvx {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            BT_A,
            BT_B,
            BT_C,
            BT_D,
            FX_L,
            FX_R,
            Start,
            VOL_L_Left,
            VOL_L_Right,
            VOL_R_Left,
            VOL_R_Right,
            Headphone,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            VOL_L,
            VOL_R,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            BT_A,
            BT_B,
            BT_C,
            BT_D,
            FX_L,
            FX_R,
            START,
            WING_LEFT_UP_R,
            WING_LEFT_UP_G,
            WING_LEFT_UP_B,
            WING_RIGHT_UP_R,
            WING_RIGHT_UP_G,
            WING_RIGHT_UP_B,
            WING_LEFT_LOW_R,
            WING_LEFT_LOW_G,
            WING_LEFT_LOW_B,
            WING_RIGHT_LOW_R,
            WING_RIGHT_LOW_G,
            WING_RIGHT_LOW_B,
            WOOFER_R,
            WOOFER_G,
            WOOFER_B,
            CONTROLLER_R,
            CONTROLLER_G,
            CONTROLLER_B,
            GENERATOR_R,
            GENERATOR_G,
            GENERATOR_B,
            POP,
            TITLE_LEFT,
            TITLE_RIGHT,
            VOLUME_SOUND,
            VOLUME_HEADPHONE,
            VOLUME_EXTERNAL,
            VOLUME_WOOFER,
            ICCR_R,
            ICCR_G,
            ICCR_B,
            TITLE_AVG_R,
            TITLE_AVG_G,
            TITLE_AVG_B,
            UPPER_LEFT_SPEAKER_AVG_R,
            UPPER_LEFT_SPEAKER_AVG_G,
            UPPER_LEFT_SPEAKER_AVG_B,
            UPPER_RIGHT_SPEAKER_AVG_R,
            UPPER_RIGHT_SPEAKER_AVG_G,
            UPPER_RIGHT_SPEAKER_AVG_B,
            LEFT_WING_AVG_R,
            LEFT_WING_AVG_G,
            LEFT_WING_AVG_B,
            RIGHT_WING_AVG_R,
            RIGHT_WING_AVG_G,
            RIGHT_WING_AVG_B,
            LOWER_LEFT_SPEAKER_AVG_R,
            LOWER_LEFT_SPEAKER_AVG_G,
            LOWER_LEFT_SPEAKER_AVG_B,
            LOWER_RIGHT_SPEAKER_AVG_R,
            LOWER_RIGHT_SPEAKER_AVG_G,
            LOWER_RIGHT_SPEAKER_AVG_B,
            CONTROL_PANEL_AVG_R,
            CONTROL_PANEL_AVG_G,
            CONTROL_PANEL_AVG_B,
            WOOFER_AVG_R,
            WOOFER_AVG_G,
            WOOFER_AVG_B,
            V_UNIT_AVG_R,
            V_UNIT_AVG_G,
            V_UNIT_AVG_B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
