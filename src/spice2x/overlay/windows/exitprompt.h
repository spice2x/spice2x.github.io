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

    private:
        void build_button(Window *window, std::string label, const ImVec2 &size, NextItem next, bool is_toggle=true);
    };
}
