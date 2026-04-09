#pragma once

#include "avs/game.h"
#include "games/game.h"

namespace games::popn {

    static inline bool is_pikapika_model() {
        return (avs::game::is_model("M39") && avs::game::SPEC[0] == 'D');
    }

    class POPNGame : public games::Game {
    public:
        POPNGame();
        ~POPNGame() override;
        virtual void pre_attach() override;
        virtual void attach() override;
    };
}
