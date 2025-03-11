#pragma once

#include "games/game.h"

namespace games::bc {
    class BCGame : public games::Game {
    public:
        BCGame() : Game("Busou Shinki: Armored Princess Battle Conductor") {}

        virtual void attach() override;
        virtual void detach() override;
    };
}
