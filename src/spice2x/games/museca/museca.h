#pragma once

#include "games/game.h"

namespace games::museca {

    class MusecaGame : public games::Game {
    public:
        MusecaGame();

        virtual void attach() override;
    };
}
