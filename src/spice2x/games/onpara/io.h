#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::onpara {
    namespace Buttons {
        enum {
            Service,
            Test,
            Start,
            Headphone,
        };
    }

    std::vector<Button> &get_buttons();
}
