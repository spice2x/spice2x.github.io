#pragma once

#include <string>
#include "games/game.h"

namespace games::bc {
    extern std::string BC_INJECT_ARGS;

    class BCGame : public games::Game {
    public:
        BCGame() : Game("Busou Shinki: Armored Princess Battle Conductor") {}

        virtual void attach() override;
        virtual void detach() override;
    };
}
