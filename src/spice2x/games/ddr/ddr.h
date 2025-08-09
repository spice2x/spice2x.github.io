#pragma once

#include "games/game.h"
#include <cstdint>

namespace games::ddr {

    // settings
    extern bool SDMODE;
    extern bool NO_CODEC_REGISTRATION;

    // Buffers to store RGB data for tape LEDs on gold cabinets
    const size_t TAPELED_DEVICE_COUNT = 11;
    extern uint8_t DDR_TAPELEDS[TAPELED_DEVICE_COUNT][50][3];

    class DDRGame : public games::Game {
    public:
        DDRGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void detach() override;

    private:
        void register_codecs();
    };
}
