#pragma once

#include "games/game.h"

namespace games::cardconnect {
    extern std::string CARDCONNECT_INJECT_ARGS;

    class CardConnectGame : public games::Game {
    public:
        CardConnectGame() : Game("Card Connect") {}

        virtual void attach() override;
        virtual void detach() override;
    };
}
