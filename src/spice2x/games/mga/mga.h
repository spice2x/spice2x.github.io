#pragma once

#include "games/game.h"

namespace games::mga {

    class MGAGame : public games::Game {
    public:
        MGAGame();
        virtual void attach() override;
        virtual void detach() override;
    };
}
