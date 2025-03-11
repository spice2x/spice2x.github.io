#pragma once

#include "games/game.h"

namespace games::loveplus {

    // touch stuff
    void touch_update();

    class LovePlusGame : public games::Game {
    public:
        LovePlusGame();
        virtual void attach() override;
        virtual void detach() override;
    };
}
