#pragma once

#include "games/game.h"

namespace games::jb {

    enum JubeatTouchAlgorithm {
        Legacy,
        Improved,
        AcAccurate
    };

    // touch stuff
    extern JubeatTouchAlgorithm TOUCH_ALGORITHM;
    extern bool TOUCH_STATE[16];
    void touch_update();

    class JBGame : public games::Game {
    public:
        JBGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void detach() override;
    };
}
