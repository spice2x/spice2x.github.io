#pragma once

#include <vector>

#include "cfg/api.h"

namespace games::udn {
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            Start,
            Up,
            Down,
            Left,
            Right,
        };
    }

    namespace Lights {
        enum udn_lights_t {
            CardReaderR,
            CardReaderG,
            CardReaderB,
            Start,
            Up,
            Down,
            Left,
            Right,
            TitleR,
            TitleG,
            TitleB,
            PillarRightTopR,
            PillarRightTopG,
            PillarRightTopB,
            PillarRightBottomR,
            PillarRightBottomG,
            PillarRightBottomB,
            StageLeftR,
            StageLeftG,
            StageLeftB,
            CabinetLeftR,
            CabinetLeftG,
            CabinetLeftB,
            PillarLeftTopR,
            PillarLeftTopG,
            PillarLeftTopB,
            PillarLeftBottomR,
            PillarLeftBottomG,
            PillarLeftBottomB,
            StageRightR,
            StageRightG,
            StageRightB,
            CabinetRightR,
            CabinetRightG,
            CabinetRightB,
        };
    }

    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Light> &get_lights();
}
