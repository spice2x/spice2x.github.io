#pragma once

#include "games/game.h"

namespace games::nost {

    extern bool ENABLE_POKE;

    class NostGame : public games::Game {
    public:
        NostGame();
        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
