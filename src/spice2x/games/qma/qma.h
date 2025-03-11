#pragma once

#include "games/game.h"

namespace games::qma {

    class QMAGame : public games::Game {
    public:
        QMAGame();
        virtual void pre_attach() override;
        virtual void attach() override;
    };
}
