#pragma once

#include "games/game.h"

namespace games::onpara {
    class OnparaGame : public games::Game {
    public:
        OnparaGame();
        virtual void attach() override;
        virtual void detach() override;
    };
}