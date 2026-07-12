#pragma once

#include <windows.h>

#include "games/game.h"

namespace games::jb {

    enum JubeatTouchAlgorithm {
        Legacy,
        Improved,
        AcAccurate
    };

    // touch stuff
    extern JubeatTouchAlgorithm TOUCH_ALGORITHM;
    extern bool TOUCH_STATE[16];
    extern bool TOUCH_DEBUG_OVERLAY;
    void touch_update();

    // whether the debug boundary boxes should currently be drawn
    bool touch_debug_overlay_enabled();

    // draw the 4x4 touch boundary boxes (as computed by the active touch algorithm)
    // onto the given device context of the spice touch overlay window
    void touch_draw_debug_overlay(HDC hdc);

    class JBGame : public games::Game {
    public:
        JBGame();
        virtual void pre_attach() override;
        virtual void attach() override;
        virtual void detach() override;
    };
}
