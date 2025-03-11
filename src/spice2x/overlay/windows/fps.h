#pragma once

#include <chrono>
#include "overlay/window.h"

namespace overlay::windows {

    class FPS : public Window {
    private:
        std::chrono::system_clock::time_point start_time;

    public:

        FPS(SpiceOverlay *overlay);

        void calculate_initial_window() override;
        void build_content() override;

        bool should_flip = false;
    };
}
