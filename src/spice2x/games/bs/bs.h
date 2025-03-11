#pragma once

#include "games/game.h"

namespace games::bs {

    class BSGame : public games::Game {
    public:
        BSGame();
        virtual void attach() override;
    };
}
