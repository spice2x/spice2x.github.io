#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::pc {
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
            FaderL_Left,
            FaderL_Right,
            FaderR_Left,
            FaderR_Right,
            Headphone
        };
    }

    namespace Analogs {
        enum {
            FaderL,
            FaderR,
        };
    }

    namespace Lights {
        typedef enum {
            IC_READER_R,
            IC_READER_G,
            IC_READER_B,

            Button1_R,
            Button1_G,
            Button1_B,

            Button2_R,
            Button2_G,
            Button2_B,

            Button3_R,
            Button3_G,
            Button3_B,

            Button4_R,
            Button4_G,
            Button4_B,

            Button5_R,
            Button5_G,
            Button5_B,

            Button6_R,
            Button6_G,
            Button6_B,
           
            Button7_R,
            Button7_G,
            Button7_B,

            Button8_R,
            Button8_G,
            Button8_B,

            Button9_R,
            Button9_G,
            Button9_B,

            Button10_R,
            Button10_G,
            Button10_B,

            Button11_R,
            Button11_G,
            Button11_B,

            Button12_R,
            Button12_G,
            Button12_B
            
        } pc_lights_t;
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
