#pragma once

#include "games/game.h"

namespace games::loveplus {

    extern bool CAMERA_ENABLE;

    // touch stuff
    void touch_update();

    class LovePlusGame : public games::Game {
    public:
        LovePlusGame();
        virtual ~LovePlusGame() override;
        virtual void attach() override;
        virtual void detach() override;
    };
}
