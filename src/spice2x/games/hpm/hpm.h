#pragma once

#include "games/game.h"

namespace games::hpm {

    class HPMGame : public games::Game {
    public:
        HPMGame();

        virtual void attach() override;
    };
}
