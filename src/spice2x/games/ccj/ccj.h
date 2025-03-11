#pragma once

#include "games/game.h"

namespace games::ccj {
    extern std::string CCJ_INJECT_ARGS;

    class CCJGame : public games::Game {
    public:
        CCJGame() : Game("Chase Chase Jokers") {}

        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
