#undef CINTERFACE

#include "avs/game.h"
#include "popn_sub.h"
#include "games/popn/popn.h"
#include "hooks/graphics/graphics.h"
#include "touch/touch.h"

namespace overlay::windows {

    PopnSubScreen::PopnSubScreen(SpiceOverlay *overlay) : GenericSubScreen(overlay) {
        this->title = "Pop'n Subscreen";

        if (!games::popn::is_pikapika_model()) {
            this->disabled_message = "Game did not launch as Pikapika Pop-kun (invalid <spec>)!";
        } else if (games::popn::SHOW_PIKA_MONITOR_WARNING) {
            this->disabled_message = "Subscreen overlay is not compatible with -dxmainadapter option, use -mainmonitor instead";
        } else if (GRAPHICS_WINDOWED && !GRAPHICS_PREVENT_SECONDARY_WINDOW) {
            this->disabled_message = "Subscren overlay was not enabled in spicecfg. Use the second window (ALT+TAB).";
        }

        this->resize_callback = keep_16_by_10;
        float size = 0.5f;
        this->init_size = ImVec2(
            ImGui::GetIO().DisplaySize.x * size,
            (ImGui::GetIO().DisplaySize.x * size * 10 / 16) + ImGui::GetFrameHeight());

        this->size_max = ImVec2(
            ImGui::GetIO().DisplaySize.x - ImGui::GetFrameHeight() * 2,
            ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight() * 2);

        // middle / bottom
        this->init_pos = ImVec2(
            ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
            ImGui::GetIO().DisplaySize.y - this->init_size.y - (ImGui::GetFrameHeight() / 2));
    }

    void PopnSubScreen::touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) {
        if (!this->get_active()) {
            return;
        }

        if (GRAPHICS_WINDOWED) {
            // Touch needs to be registered on global coords
            *x_out = SPICETOUCH_TOUCH_X + xy_in.x * SPICETOUCH_TOUCH_WIDTH;
            *y_out = SPICETOUCH_TOUCH_Y + xy_in.y * SPICETOUCH_TOUCH_HEIGHT;
        } else {
            // Fullscreen mode, scale to game coords
            *x_out = xy_in.x * ImGui::GetIO().DisplaySize.x;
            *y_out = xy_in.y * ImGui::GetIO().DisplaySize.y;
        }
    }
}
