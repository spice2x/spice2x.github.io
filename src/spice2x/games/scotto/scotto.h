#pragma once

#include "games/game.h"

namespace games::scotto {

    class ScottoGame : public games::Game {
    public:
        ScottoGame();
        virtual void attach() override;
        virtual void detach() override;
    };
}
