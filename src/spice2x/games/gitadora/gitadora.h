#pragma once

#include <optional>
#include <utility>

#include <windows.h>
#include <mmreg.h>

#include "avs/game.h"
#include "games/game.h"
#include "util/socd_cleaner.h"

namespace games::gitadora {

    // settings
    extern bool TWOCHANNEL;
    extern bool DISABLE_FRAME_LIMITER;
    extern std::optional<unsigned int> CAB_TYPE;
    extern bool P1_LEFTY;
    extern bool P2_LEFTY;
    extern std::optional<std::string> SUBSCREEN_OVERLAY_SIZE;
    extern std::optional<socd::SocdAlgorithm> PICK_ALGO;
    extern std::optional<uint8_t> ARENA_WINDOW_COUNT;
    extern bool ARENA_TWO_HEAD_EXCLUSIVE;
    extern bool ARENA_SUBSCREEN_LANDSCAPE;
    extern std::optional<std::string> ASIO_DRIVER;
    extern bool ALLOW_REALTEK_AUDIO;
    extern bool NATIVE_TOUCH;

    // arena SMALL subscreen (touch panel) resolution
    static constexpr int ARENA_SUBSCREEN_WIDTH = 800;
    static constexpr int ARENA_SUBSCREEN_HEIGHT = 1280;

    // used when a landscape monitor drives the SMALL head and -forceressub is unset
    static constexpr int ARENA_SUBSCREEN_LANDSCAPE_WIDTH = 1920;
    static constexpr int ARENA_SUBSCREEN_LANDSCAPE_HEIGHT = 1080;

    // resolution the SMALL head actually runs at; the portrait panel size unless
    // -forceressub or the landscape option overrides it
    std::pair<UINT, UINT> arena_subscreen_host_size();

    // area of the host the portrait subscreen occupies, centered and aspect-preserved;
    // whatever is left over on either side stays black
    RECT arena_subscreen_content_rect(LONG host_width, LONG host_height);

    class GitaDoraGame : public games::Game {
    public:
        GitaDoraGame();
        virtual void pre_attach() override;
        virtual void attach() override;
    };

    void fix_audio_channel_mask(WAVEFORMATEX *format);

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

    static inline bool is_player_lefty(size_t player) {
        if (player == 0) {
            return P1_LEFTY;
        } else if (player == 1) {
            return P2_LEFTY;
        }
        return false;
    }
}
