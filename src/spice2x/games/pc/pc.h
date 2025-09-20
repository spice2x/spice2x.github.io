#pragma once

#include "games/game.h"

namespace games::pc {
    extern std::string PC_INJECT_ARGS;
    extern bool PC_NO_IO;
    extern bool PC_KNOB_MODE;

    class PCGame : public games::Game {
    public:
        PCGame() : Game("Polaris Chord") {}

        virtual void attach() override;
        virtual void detach() override;
    };
}
