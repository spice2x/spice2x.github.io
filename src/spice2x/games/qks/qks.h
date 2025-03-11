#pragma once

#include "games/game.h"

namespace games::qks {
    extern std::string QKS_INJECT_ARGS;

    class QKSGame : public games::Game {
    public:
        QKSGame() : Game("QuizKnock STADIUM") {}

        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
