#undef CINTERFACE

#include "avs/game.h"
#include "sdvx_sub.h"
#include "games/sdvx/sdvx.h"
#include "hooks/graphics/graphics.h"

namespace overlay::windows {

    SDVXSubScreen::SDVXSubScreen(SpiceOverlay *overlay) : GenericSubScreen(overlay) {
        this->title = "SDVX Sub Screen";

        bool isValkyrieCabinetMode = avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H';
        if (!isValkyrieCabinetMode) {
            this->disabled_message = "Valkyrie Model mode is not enabled!";
        } else if (GRAPHICS_WINDOWED) {
            if (GRAPHICS_PREVENT_SECONDARY_WINDOW) {
                this->disabled_message = "Subscreen has been disabled by the user (-sdvxnosub).";
            } else {
                this->disabled_message = "Overlay unavailable in windowed mode! Use the second window instead.";
            }
        }

        const auto padding = ImGui::GetFrameHeight() / 2;

        this->init_size = ImVec2(ImGui::GetIO().DisplaySize.x - (padding * 2), 0.f);
        this->init_size.y = (this->init_size.x * 9 / 16) + ImGui::GetFrameHeight();

        this->init_pos = ImVec2(ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2, 0);
        switch (games::sdvx::OVERLAY_POS) {
            case games::sdvx::SDVX_OVERLAY_TOP:
                this->init_pos.y = padding;
                break;
            case games::sdvx::SDVX_OVERLAY_BOTTOM:
                this->init_pos.y = ImGui::GetIO().DisplaySize.y - this->init_size.y - padding;
                break;
            case games::sdvx::SDVX_OVERLAY_MIDDLE:
            default:
                this->init_pos.y = (ImGui::GetIO().DisplaySize.y / 2) - (this->init_size.y / 2);
                break;
        }
    }

    void SDVXSubScreen::touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) {
        if (!this->get_active()) {
            return;
        }
        
        // SDVX Valkyrie cab needs math to make things work
        // what the game expects from touch screen:
        // [1080, 0     1920, 1080]
        // [0, 0           0, 1920]

        // actual cursor position given by x_in (on the overlay, normalized):
        // [0, 0           1, 0]
        // [0, 1           1, 1]

        // Basically, the game operates on rotated coordinates, but on top of that,
        // it needs to be adjusted so they match up with the subscreen overlay.

        // so the math is:
        // Xin(0, 1) => Yout(0, 1920)
        // Yin(0, 1) => Xout(1080, 0)

        *x_out = ImGui::GetIO().DisplaySize.x * (1.f - xy_in.y);
        *y_out = ImGui::GetIO().DisplaySize.y * xy_in.x;
    }
}
