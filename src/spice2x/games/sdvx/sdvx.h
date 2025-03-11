
#pragma once

#include <optional>
#include <cstdint>

#include "games/game.h"

namespace games::sdvx {

    enum SdvxOverlayPosition {
        SDVX_OVERLAY_TOP,
        SDVX_OVERLAY_MIDDLE,
        SDVX_OVERLAY_BOTTOM
    };

    // settings
    extern bool DISABLECAMS;
    extern bool NATIVETOUCH;
    extern uint8_t DIGITAL_KNOB_SENS;
    extern std::optional<std::string> ASIO_DRIVER;
    extern bool BI2X_INITIALIZED;
    extern SdvxOverlayPosition OVERLAY_POS;

    class SDVXGame : public games::Game {
    public:
        SDVXGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    };
}
