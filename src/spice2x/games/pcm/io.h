#pragma once

#include <vector>

#include "cfg/api.h"

namespace games::pcm {
    namespace Buttons {
        enum {
            Service,
            Test,
            Insert1000YenBill,
            Insert2000YenBill,
            Insert5000YenBill,
            Insert10000YenBill,
        };
    }

    std::vector <Button> &get_buttons();
};
