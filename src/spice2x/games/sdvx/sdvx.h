
#pragma once

#include <optional>
#include <cstdint>

#include "games/game.h"

namespace games::sdvx {

    enum SdvxOverlayPosition {
        SDVX_OVERLAY_TOP,
        SDVX_OVERLAY_MIDDLE,
        SDVX_OVERLAY_BOTTOM,
        SDVX_OVERLAY_BOTTOM_LEFT,
        SDVX_OVERLAY_BOTTOM_RIGHT
    };

    // settings
    extern bool NATIVETOUCH;
    extern uint8_t DIGITAL_KNOB_SENS;
    extern std::optional<std::string> ASIO_DRIVER;
    extern bool BI2X_INITIALIZED;
    extern SdvxOverlayPosition OVERLAY_POS;

    // states
    extern bool SHOW_VM_MONITOR_WARNING;

    class SDVXGame : public games::Game {
    public:
        SDVXGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    private:
        // don't be tempted to make this into a public or a global variable
        // various modules need to check if VM mode is active and they should
        // not depend on the fact that SDVX module is active (since it can be
        // inactive on cabs or partial I/O setup)
        bool VALKYRIE_MODEL = false;
    };
}
