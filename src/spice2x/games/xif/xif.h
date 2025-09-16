#pragma once

#include "games/game.h"

namespace games::xif {
    extern std::string XIF_INJECT_ARGS;

    class XIFGame : public games::Game {
    public:
        XIFGame() : Game("Polaris Chord") {}

        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
