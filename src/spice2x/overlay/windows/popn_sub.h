#pragma once

#include <windows.h>

#include "overlay/window.h"
#include "overlay/windows/generic_sub.h"

namespace overlay::windows {

    class PopnSubScreen : public GenericSubScreen {
    public:
        PopnSubScreen(SpiceOverlay *overlay);

    protected:
        void touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) override;

    private:
        static void keep_16_by_10(ImGuiSizeCallbackData* data) {
            data->DesiredSize.y = (data->DesiredSize.x * 10.f / 16.f) + ImGui::GetFrameHeight();
        }
    };
}
