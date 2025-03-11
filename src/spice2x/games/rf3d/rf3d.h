#pragma once

#include "games/game.h"

namespace games::rf3d {

    class RF3DGame : public games::Game {
    public:
        RF3DGame();
        virtual void attach() override;
        virtual void detach() override;
    };
}
