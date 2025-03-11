#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::bbc {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            P1_R,
            P1_G,
            P1_B,
            P1_DiskMinus,
            P1_DiskPlus,
            P1_DiskSlowdown,
            P2_R,
            P2_G,
            P2_B,
            P2_DiskMinus,
            P2_DiskPlus,
            P2_DiskSlowdown,
            P3_R,
            P3_G,
            P3_B,
            P3_DiskMinus,
            P3_DiskPlus,
            P3_DiskSlowdown,
            P4_R,
            P4_G,
            P4_B,
            P4_DiskMinus,
            P4_DiskPlus,
            P4_DiskSlowdown,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            P1_Disk,
            P2_Disk,
            P3_Disk,
            P4_Disk,
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            P1_R,
            P1_B,
            P1_DISC_R,
            P1_DISC_G,
            P1_DISC_B,
            P2_R,
            P2_B,
            P2_DISC_R,
            P2_DISC_G,
            P2_DISC_B,
            P3_R,
            P3_B,
            P3_DISC_R,
            P3_DISC_G,
            P3_DISC_B,
            P4_R,
            P4_B,
            P4_DISC_R,
            P4_DISC_G,
            P4_DISC_B,
            IC_CARD_R,
            IC_CARD_G,
            IC_CARD_B,
            UNDER_LED1_R,
            UNDER_LED1_G,
            UNDER_LED1_B,
            UNDER_LED2_R,
            UNDER_LED2_G,
            UNDER_LED2_B,
            UNDER_LED3_R,
            UNDER_LED3_G,
            UNDER_LED3_B,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
