#pragma once

#include "games/game.h"

namespace games::pcm {
    class PCMGame : public Game {
    public:
        PCMGame();

        void attach() override;
    };
}