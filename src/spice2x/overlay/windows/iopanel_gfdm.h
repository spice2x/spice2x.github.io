#pragma once

#include "overlay/window.h"
#include "overlay/windows/iopanel.h"
#include "cfg/button.h"

namespace overlay::windows {

    class GitadoraIOPanel : public IOPanel {
    public:
        GitadoraIOPanel(SpiceOverlay *overlay);

    protected:
        void build_io_panel() override;

    private:
        void find_gfdm_buttons();
        void draw_buttons(const int player);
        void draw_sliders(const int player);

        bool two_players;
        bool has_guitar_knobs;

        Button *start[2];
        Button *help[2];
        Button *up[2];
        Button *down[2];
        Button *left[2];
        Button *right[2];

        Light *start_light[2];
        Light *help_light[2];
        Light *updown_light[2];
        Light *leftright_light[2];
    };
}
