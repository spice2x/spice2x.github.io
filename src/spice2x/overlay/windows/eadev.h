#pragma once

#include "overlay/window.h"

namespace overlay::windows {

    class EADevWindow : public Window {
    public:

        EADevWindow(SpiceOverlay *overlay);
        ~EADevWindow() override;

        void build_content() override;
        static void automap_hook(void *user, const char *data);

    private:

        bool automap_autoscroll = true;
        bool automap_scroll_to_bottom = false;
        std::vector<std::string> automap_data;
    };
}
