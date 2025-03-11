#pragma once

#include "overlay/window.h"

namespace overlay::windows {

    class WndManagerWindow : public Window {
    public:

        WndManagerWindow(SpiceOverlay *overlay);
        ~WndManagerWindow() override;

        void build_content() override;

    private:

        int window_current = -1;
    };
}
