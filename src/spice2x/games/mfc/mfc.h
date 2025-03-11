#pragma once

#include "games/game.h"

namespace games::mfc {

    struct joystick_state {
        bool up = false;
        bool down = false;
        bool start = false;
        bool service = false;
        bool test = false;
    };

    class MFCGame : public games::Game {
    public:
        MFCGame();
        virtual void attach() override;
    };
}
