#pragma once

#include <chrono>
#include "overlay/window.h"

namespace overlay::windows {

    enum class NextItem {
        NEW_LINE,
        SAME_LINE
    };

    class ExitPrompt : public Window {
    public:
        ExitPrompt(SpiceOverlay *overlay);
        void build_content() override;
        void update() override;

    private:
        void build_button(Window *window, std::string label, const ImVec2 &size, NextItem next);

        // latch so the popup is opened once per activation; reset in update()
        // whenever the menu is inactive
        bool popup_opened = false;
    };
}
