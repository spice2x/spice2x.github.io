#pragma once

#include "games/game.h"

namespace games::dea {

    class DEAGame : public games::Game {
    public:
        DEAGame();
        virtual void attach() override;
    };
}
