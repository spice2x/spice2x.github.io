
#pragma once

#include <optional>
#include <cstdint>

#include "avs/game.h"
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
    extern SdvxOverlayPosition OVERLAY_POS;

    // states
    extern bool SHOW_VM_MONITOR_WARNING;

    static inline bool is_valkyrie_model() {
        return (
            avs::game::is_model("KFC") &&
            (avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H')
            );
    }

    class SDVXGame : public games::Game {
    public:
        SDVXGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void post_attach() override;
        virtual void detach() override;
    private:
        void detect_sound_output_device();
    };
}
