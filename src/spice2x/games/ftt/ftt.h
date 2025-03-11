#pragma once

#include "games/game.h"

namespace games::ftt {

    class FTTGame : public games::Game {
    public:
        FTTGame();
        virtual void attach() override;
    };
}
