#pragma once

#include "games/game.h"

namespace games::popn {

    class POPNGame : public games::Game {
    public:
        POPNGame();
        ~POPNGame() override;
        virtual void pre_attach() override;
        virtual void attach() override;
    };
}
