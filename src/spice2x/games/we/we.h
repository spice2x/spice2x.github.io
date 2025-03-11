#pragma once

#include "games/game.h"

namespace games::we {

    class WEGame : public games::Game {
    public:
        WEGame();

        virtual void attach() override;
        virtual void detach() override;
    };
}
