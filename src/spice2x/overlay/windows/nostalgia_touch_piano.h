#pragma once

#include "overlay/window.h"

namespace overlay::windows {

    // persistent mode control rendered independently of the main overlay visibility
    class NostalgiaTouchPiano : public Window {
    public:
        explicit NostalgiaTouchPiano(SpiceOverlay *overlay);

        void calculate_initial_window() override;
        void build_content() override;
    };
}