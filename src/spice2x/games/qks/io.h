#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::qks {
    namespace Buttons {
        enum {
            Test,
            Service,
            Coin,
            QButton,
            QButton1,
            QButton2,
            QButton3,
            JackDetect,
            MicDetect
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
}
