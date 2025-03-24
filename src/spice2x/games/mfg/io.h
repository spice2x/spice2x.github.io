#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::mfg {
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            ActionButton
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
}
