#pragma once

#include "games/game.h"

namespace games::otoca {

    extern bool BYPASS_CAMERA;

    class OtocaGame : public games::Game {
    public:
        OtocaGame();
        ~OtocaGame() override;

        virtual void attach() override;
    };
}
