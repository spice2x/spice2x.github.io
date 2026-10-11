#pragma once

#include <vector>

#include "cfg/api.h"

namespace games::cardconnect {
    namespace Buttons {
        enum {
            Test,
            Service,
            CoinMech,
        };
    }

    namespace Lights {
        enum {
            PhotoLight,
            CardReaderR,
            CardReaderG,
            CardReaderB,
            SideLeftR,
            SideLeftG,
            SideLeftB,
            SideRightR,
            SideRightG,
            SideRightB,
        };
    }

    std::vector<Button> &get_buttons();
    std::vector<Light> &get_lights();
}
