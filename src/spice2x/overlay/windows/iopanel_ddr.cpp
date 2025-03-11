#include "iopanel_ddr.h"
#include "games/io.h"
#include "games/ddr/io.h"
#include "misc/eamuse.h"
#include "util/logging.h"

namespace overlay::windows {

    DDRIOPanel::DDRIOPanel(SpiceOverlay *overlay) : IOPanel(overlay) {
        this->title = "DDR IO Panel";
        find_ddr_buttons();
    }

    void DDRIOPanel::find_ddr_buttons() {
        const auto buttons = games::get_buttons(eamuse_get_game());
        const auto lights = games::get_lights(eamuse_get_game());

        // SD cabs don't have lights for these buttons, so just use the HD ones

        this->start[0] = &(*buttons)[games::ddr::Buttons::P1_START];
        this->up[0] = &(*buttons)[games::ddr::Buttons::P1_MENU_UP];
        this->down[0] = &(*buttons)[games::ddr::Buttons::P1_MENU_DOWN];
        this->left[0] = &(*buttons)[games::ddr::Buttons::P1_MENU_LEFT];
        this->right[0] = &(*buttons)[games::ddr::Buttons::P1_MENU_RIGHT];
        this->start_light[0] = &(*lights)[games::ddr::Lights::HD_P1_START];
        this->updown_light[0] = &(*lights)[games::ddr::Lights::HD_P1_UP_DOWN];
        this->leftright_light[0] = &(*lights)[games::ddr::Lights::HD_P1_LEFT_RIGHT];

        this->start[1] = &(*buttons)[games::ddr::Buttons::P2_START];
        this->up[1] = &(*buttons)[games::ddr::Buttons::P2_MENU_UP];
        this->down[1] = &(*buttons)[games::ddr::Buttons::P2_MENU_DOWN];
        this->left[1] = &(*buttons)[games::ddr::Buttons::P2_MENU_LEFT];
        this->right[1] = &(*buttons)[games::ddr::Buttons::P2_MENU_RIGHT];
        this->start_light[1] = &(*lights)[games::ddr::Lights::HD_P2_START];
        this->updown_light[1] = &(*lights)[games::ddr::Lights::HD_P2_UP_DOWN];
        this->leftright_light[1] = &(*lights)[games::ddr::Lights::HD_P2_LEFT_RIGHT];
    }

    void DDRIOPanel::build_io_panel() {
        ImGui::Dummy(ImVec2(12, 0));

        ImGui::SameLine();
        this->draw_buttons(0);

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(12, 0));

        ImGui::SameLine();
        this->draw_buttons(1);
    }

    void DDRIOPanel::draw_buttons(const int p) {
        // 2x2
        const ImVec2 start_button_size(
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight(),
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight()
            );
        // 2x1
        const ImVec2 updown_size(start_button_size.x, ImGui::GetFrameHeight());
        // 1x2
        const ImVec2 leftright_size(ImGui::GetFrameHeight(), start_button_size.y);

        ImGui::BeginGroup();
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFrameHeightWithSpacing());
            this->build_button("<", leftright_size, this->left[p], nullptr, this->leftright_light[p]);
        }
        ImGui::EndGroup();

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.y);

        ImGui::BeginGroup();
        {
            this->build_button("^", updown_size, this->up[p], nullptr, this->updown_light[p]);
            const char *label;
            if (p == 0) {
                label = " 1 P\nStart";
            } else {
                label = " 2 P\nStart";
            }
            this->build_button(label, start_button_size, this->start[p], nullptr, this->start_light[p]);
            this->build_button("v", updown_size, this->down[p], nullptr, this->updown_light[p]);
        }
        ImGui::EndGroup();

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.y);

        ImGui::BeginGroup();
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFrameHeightWithSpacing());
            this->build_button(">", leftright_size, this->right[p], nullptr, this->leftright_light[p]);
        }
        ImGui::EndGroup();
    }
}
