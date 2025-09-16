#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::pc {
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Lane1,
            Lane2,
            Lane3,
            Lane4,
            Lane5,
            Lane6,
            Lane7,
            Lane8,
            Lane9,
            Lane10,
            Lane11,
            Lane12,
            FaderL_Left,
            FaderL_Right,
            FaderR_Left,
            FaderR_Right,
            Headphone,
            Recorder
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
