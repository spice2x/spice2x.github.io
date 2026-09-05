#pragma once

#include "games/game.h"

namespace games::udn {
    extern std::string UDN_INJECT_ARGS;
    extern bool UDN_NO_IO;
    extern bool UDN_ENABLE_MIDI;

    class UDNGame : public games::Game {
    public:
        UDNGame() : Game("DANCE aROUND") {}

        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void detach() override;
    };
}
