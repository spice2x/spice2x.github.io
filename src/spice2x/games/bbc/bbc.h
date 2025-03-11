#pragma once

#include "games/game.h"

namespace games::bbc {

    class BBCGame : public games::Game {
    public:
        BBCGame();

        virtual void attach() override;
    };
}
