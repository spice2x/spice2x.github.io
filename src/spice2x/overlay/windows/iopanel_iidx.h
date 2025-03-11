#pragma once

#include "overlay/window.h"
#include "overlay/windows/iopanel.h"
#include "cfg/button.h"

namespace overlay::windows {

    class IIDXIOPanel : public IOPanel {
    public:
        IIDXIOPanel(SpiceOverlay *overlay);

    protected:
        void build_io_panel() override;

    private:
        void find_iidx_buttons();
        void draw_buttons();
        void draw_sliders();

        Button *start_1p = nullptr;
        Light *start_1p_light = nullptr;

        Button *start_2p = nullptr;
        Light *start_2p_light = nullptr;

        Button *effect_on = nullptr;
        Light *effect_on_light = nullptr;

        Button *vefx = nullptr;
        Light *vefx_light = nullptr;
    };
}
