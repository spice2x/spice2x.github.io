#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::xif {
    namespace Buttons {
        enum {
            Test,
            Service,
            Coin,

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
            
            L_FaderLeft,
            L_FaderRight,
            R_FaderLeft,
            R_FaderRight,

            Headphone,
            Recorder
        };
    }

    namespace Analogs {
        enum
        {
            L_Fader,
            R_Fader,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::vector<Analog> &get_analogs();
    std::string get_buttons_help();
    std::string get_analogs_help();
}
