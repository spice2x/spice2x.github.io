#include "avs/game.h"
#include "gfdm_sub.h"
#include "games/gitadora/gitadora.h"
#include "hooks/graphics/graphics.h"

namespace overlay::windows {

    GitaDoraSubScreen::GitaDoraSubScreen(SpiceOverlay *overlay) : GenericSubScreen(overlay) {
        this->title = "GITADORA Subscreen";

        if (!games::gitadora::is_arena_model()) {
            this->disabled_message = "Requires Arena Model!";
        }

        this->resize_callback = keep_10_by_16;

        const float size = 0.5f;
        const float height = ImGui::GetIO().DisplaySize.y * size;
        const float width = height * 10 / 16;
        this->init_size = ImVec2(width, height + ImGui::GetFrameHeight());

        // middle / bottom
        // this->init_pos = ImVec2(
        //     ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
        //     ImGui::GetIO().DisplaySize.y - this->init_size.y - (ImGui::GetFrameHeight() / 2));

        // bottom right
        this->init_pos = ImVec2(
            ImGui::GetIO().DisplaySize.x * 0.99f - this->init_size.x,
            ImGui::GetIO().DisplaySize.y - this->init_size.y - (ImGui::GetFrameHeight() / 2));
    }

    void GitaDoraSubScreen::touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) {
        if (!this->get_active()) {
            return;
        }
    }
}
