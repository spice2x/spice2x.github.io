#pragma once

#include "games/game.h"

namespace games::silentscope {

    class SilentScopeGame : public games::Game {
    public:
        SilentScopeGame();

        void attach() override;
        void detach() override;
    };
}
