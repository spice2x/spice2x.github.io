#pragma once

#include <cstdint>

#include "games/game.h"

namespace games::rb {

    extern uint16_t TOUCH_SCALING;

    class RBGame : public games::Game {
    public:
        RBGame();
        virtual void attach();
        virtual void detach();
    };
}
