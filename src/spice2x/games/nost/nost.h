#pragma once

#include "games/game.h"

namespace games::nost {

    extern bool ENABLE_POKE;
    extern bool ENABLE_TOUCH_MODE;

    class NostGame : public games::Game {
    public:
        NostGame();
        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
