#pragma once

#include "games/game.h"
#include "games/jb/jb_touch.h"

namespace games::jb {

    class JBGame : public games::Game {
    public:
        JBGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void detach() override;
    };
}
