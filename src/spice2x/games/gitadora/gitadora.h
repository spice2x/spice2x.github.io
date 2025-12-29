#pragma once

#include <optional>

#include "avs/game.h"
#include "games/game.h"

namespace games::gitadora {

    // settings
    extern bool TWOCHANNEL;
    extern std::optional<unsigned int> CAB_TYPE;

    class GitaDoraGame : public games::Game {
    public:
        GitaDoraGame();
        virtual void pre_attach() override;
        virtual void attach() override;
    };

    static inline bool is_drum() {
        return (
            avs::game::is_model({ "J32", "K32", "L32" }) ||
            (avs::game::is_model("M32") && (avs::game::SPEC[0] == 'B' || avs::game::SPEC[0] == 'D'))
            );
    }
    static inline bool is_guitar() {
        return (
            avs::game::is_model({ "J33", "K33", "L33" }) ||
            (avs::game::is_model("M32") && (avs::game::SPEC[0] == 'A' || avs::game::SPEC[0] == 'C'))
            );
    }

    static inline bool is_arena_model() {
        return (
            avs::game::is_model("M32") &&
            (avs::game::SPEC[0] == 'C' || avs::game::SPEC[0] == 'D')
            );
    }

}
