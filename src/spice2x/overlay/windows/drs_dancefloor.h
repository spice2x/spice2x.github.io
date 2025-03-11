#pragma once

#include "games/drs/drs.h"
#include "overlay/window.h"

namespace overlay::windows {

    class DRSDanceFloorDisplay : public Window {
    public:
        DRSDanceFloorDisplay(SpiceOverlay *overlay);
        void calculate_initial_window() override;
        void build_content() override;

    private:
        static void keep_aspect_ratio(ImGuiSizeCallbackData* data) {
            const float ratio = (float)DRS_TAPELED_ROWS / (float)DRS_TAPELED_COLS;
            data->DesiredSize.y = (data->DesiredSize.x * ratio) + ImGui::GetFrameHeight();
        }
    };
}
