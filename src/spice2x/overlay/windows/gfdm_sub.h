#pragma once

#include "overlay/window.h"
#include "overlay/windows/generic_sub.h"

namespace overlay::windows {

    class GitaDoraSubScreen : public GenericSubScreen {
    public:
        GitaDoraSubScreen(SpiceOverlay *overlay);

    protected:
        void touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) override;

    private:
        static void keep_10_by_16(ImGuiSizeCallbackData* data) {
            data->DesiredSize.y = (data->DesiredSize.x * 16.f / 10.f) + ImGui::GetFrameHeight();
        }
    };
}
