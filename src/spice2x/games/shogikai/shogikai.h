#pragma once

#include "games/game.h"

namespace games::shogikai {

    class ShogikaiGame : public games::Game {
    public:
        ShogikaiGame();

        virtual void attach() override;
        virtual void detach() override;
    };
}
