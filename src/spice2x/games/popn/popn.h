#pragma once

#include "avs/game.h"
#include "games/game.h"
#include "util/tapeled.h"

namespace games::popn {

    static inline bool is_pikapika_model() {

#if SPICE64
        return (avs::game::is_model("M39") && avs::game::SPEC[0] == 'D');
#else
        return false;
#endif

    }

#if SPICE64
    constexpr int POPN_TAPELED_TOTAL = 4;
    extern tapeledutils::tape_led TAPELED_MAPPING[POPN_TAPELED_TOTAL];
#endif

    extern bool SHOW_PIKA_MONITOR_WARNING;

    class POPNGame : public games::Game {
    public:
        POPNGame();
        ~POPNGame() override;
        virtual void pre_attach() override;
        virtual void attach() override;
    };
}
