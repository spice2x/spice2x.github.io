#pragma once

#include "games/game.h"

namespace games::mfg {
    extern std::string MFG_INJECT_ARGS;
    extern std::string MFG_CABINET_TYPE;
    extern bool MFG_NO_IO;

    class MFGGame : public games::Game {
    public:
        MFGGame() : Game("Mahjong Fight Girl") {}

        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
