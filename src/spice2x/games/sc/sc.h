#pragma once

#include "games/game.h"

namespace games::sc {

    class SCGame : public games::Game {
    public:
        SCGame();

        virtual void attach() override;
        virtual void detach() override;
    };
}
