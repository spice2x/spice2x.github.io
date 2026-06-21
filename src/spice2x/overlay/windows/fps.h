#pragma once

#include <chrono>
#include "overlay/window.h"

namespace overlay::windows {

    class FPS : public Window {
    private:
        std::chrono::system_clock::time_point start_time;

        // anchored top-left position for a window of the given size, per FPS_LOCATION
        ImVec2 anchored_pos(const ImVec2 &size) const;

    public:

        FPS(SpiceOverlay *overlay);

        void calculate_initial_window() override;
        void build_content() override;
    };
}
