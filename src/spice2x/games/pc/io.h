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
        } pc_lights_t;
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
