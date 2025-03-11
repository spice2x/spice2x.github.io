#pragma once

#include "games/game.h"

namespace games::otoca {

    class OtocaGame : public games::Game {
    public:
        OtocaGame();
        ~OtocaGame() override;

        virtual void attach() override;
    };
}
