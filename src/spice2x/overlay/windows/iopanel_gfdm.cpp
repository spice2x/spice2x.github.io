#include "iopanel_gfdm.h"
#include "avs/game.h"
#include "games/io.h"
#include "games/gitadora/gitadora.h"
#include "games/gitadora/io.h"
#include "overlay/imgui/extensions.h"
#include "misc/eamuse.h"
#include "util/logging.h"

namespace overlay::windows {

    GitadoraIOPanel::GitadoraIOPanel(SpiceOverlay *overlay) : IOPanel(overlay) {
        this->title = "GITADORA IO Panel";

        this->has_menu_controls = true;
        // by default, make a safer assumption that there are two players
        this->two_players = true;
        // by default, enable the extra input only available on DX cabs...
        this->has_guitar_knobs = true;

        // drummania can only have one player, no guitar knobs
        if (games::gitadora::is_drum()) {
            this->two_players = false;
            this->has_guitar_knobs = false;
        }

        // and cab type 3 (white cab) only has one guitar
        if (games::gitadora::CAB_TYPE.has_value() && games::gitadora::CAB_TYPE == 3) {
            this->two_players = false;
        }

        // disable guitar knobs on known non-DX cabs
        if (games::gitadora::CAB_TYPE.has_value() &&
            (games::gitadora::CAB_TYPE == 2 || games::gitadora::CAB_TYPE == 3)) {
            this->has_guitar_knobs = false;
        }

        if (games::gitadora::is_arena_model()) {
            this->has_menu_controls = false;
            this->two_players = false;
            this->has_guitar_knobs = false;
        }

        find_gfdm_buttons();
    }

    void GitadoraIOPanel::find_gfdm_buttons() {
        const auto buttons = games::get_buttons(eamuse_get_game());
        const auto lights = games::get_lights(eamuse_get_game());

        // device emulation treats drum controls to be the same as guitar 1p

        this->start[0] = &(*buttons)[games::gitadora::Buttons::GuitarP1Start];
        this->help[0] = &(*buttons)[games::gitadora::Buttons::GuitarP1Help];
        this->up[0] = &(*buttons)[games::gitadora::Buttons::GuitarP1Up];
        this->down[0] = &(*buttons)[games::gitadora::Buttons::GuitarP1Down];
        this->left[0] = &(*buttons)[games::gitadora::Buttons::GuitarP1Left];
        this->right[0] = &(*buttons)[games::gitadora::Buttons::GuitarP1Right];

        this->start_light[0] = &(*lights)[games::gitadora::Lights::P1MenuStart];
        this->help_light[0] = &(*lights)[games::gitadora::Lights::P1MenuHelp];
        this->updown_light[0] = &(*lights)[games::gitadora::Lights::P1MenuUpDown];
        this->leftright_light[0] = &(*lights)[games::gitadora::Lights::P1MenuLeftRight];

        this->start[1] = &(*buttons)[games::gitadora::Buttons::GuitarP2Start];
        this->help[1] = &(*buttons)[games::gitadora::Buttons::GuitarP2Help];
        this->up[1] = &(*buttons)[games::gitadora::Buttons::GuitarP2Up];
        this->down[1] = &(*buttons)[games::gitadora::Buttons::GuitarP2Down];
        this->left[1] = &(*buttons)[games::gitadora::Buttons::GuitarP2Left];
        this->right[1] = &(*buttons)[games::gitadora::Buttons::GuitarP2Right];

        this->start_light[1] = &(*lights)[games::gitadora::Lights::P2MenuStart];
        this->help_light[1] = &(*lights)[games::gitadora::Lights::P2MenuHelp];
        this->updown_light[1] = &(*lights)[games::gitadora::Lights::P2MenuUpDown];
        this->leftright_light[1] = &(*lights)[games::gitadora::Lights::P2MenuLeftRight];
    }

    void GitadoraIOPanel::build_io_panel() {
        ImGui::Dummy(overlay::apply_scaling_to_vector(12, 0));

        if (this->has_menu_controls) {
            ImGui::SameLine();
            ImGui::PushID("P1");
            this->draw_buttons(0);
            if (this->has_guitar_knobs) {
                ImGui::SameLine();
                this->draw_sliders(0);
            }
            ImGui::PopID();

            // draw p2 only if guitar freaks
            if (this->two_players) {
                ImGui::SameLine();
                ImGui::Dummy(overlay::apply_scaling_to_vector(12, 0));
                ImGui::SameLine();
                ImGui::PushID("P2");
                this->draw_buttons(1);
                if (this->has_guitar_knobs) {
                    ImGui::SameLine();
                    this->draw_sliders(1);
                }
                ImGui::PopID();
            }
        }

        if (games::gitadora::is_guitar()) {
            ImGui::SameLine();
            ImGui::Dummy(overlay::apply_scaling_to_vector(12, 0));
            ImGui::SameLine();
            ImGui::BeginGroup();
            {
                ImGui::Checkbox("P1 Lefty", &games::gitadora::P1_LEFTY);
                if (this->two_players) {
                    ImGui::Checkbox("P2 Lefty", &games::gitadora::P2_LEFTY);
                }
            }
            ImGui::EndGroup();
        }
    }

    void GitadoraIOPanel::draw_buttons(const int p) {
        // 2x2
        const ImVec2 start_button_size(
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight(),
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight()
            );
        // 2x1
        const ImVec2 updown_size(start_button_size.x, ImGui::GetFrameHeight());
        // 1x2
        const ImVec2 leftright_size(ImGui::GetFrameHeight(), start_button_size.y);
        // 1x1
        const ImVec2 tiny_size(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

        ImGui::BeginGroup();
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFrameHeightWithSpacing());
            this->build_button("<", leftright_size, this->left[p], nullptr, this->leftright_light[p]);
            this->build_button("+", tiny_size, this->help[p], this->start[p], nullptr);
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                ImGui::HelpTooltip("HELP + START");
            }
        }
        ImGui::EndGroup();

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.y);

        ImGui::BeginGroup();
        {
            this->build_button("^", updown_size, this->up[p], nullptr, this->updown_light[p]);
            const char *label;
            if (this->two_players) {
                if (p == 0) {
                    label = " 1 P\nStart";
                } else {
                    label = " 2 P\nStart";
                }
            } else {
                label = "Start";
            }
            this->build_button(label, start_button_size, this->start[p], nullptr, this->start_light[p]);
            this->build_button("v", updown_size, this->down[p], nullptr, this->updown_light[p]);
        }
        ImGui::EndGroup();

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.y);

        ImGui::BeginGroup();
        {
            this->build_button("?", tiny_size, this->help[p], nullptr, this->help_light[p]);
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                ImGui::HelpTooltip("HELP");
            }
            this->build_button(">", leftright_size, this->right[p], nullptr, this->leftright_light[p]);
        }
        ImGui::EndGroup();
    }

    void GitadoraIOPanel::draw_sliders(const int p) {
        const auto index =
            (p == 0) ?
            games::gitadora::Analogs::GuitarP1Knob :
            games::gitadora::Analogs::GuitarP2Knob;

        const ImVec2 slider_size(overlay::apply_scaling(32), get_suggested_height());

        // get analog
        const auto analogs = games::get_analogs(eamuse_get_game());
        auto &analog = (*analogs)[index];

        // vertical slider
        auto new_state = analog.override_enabled ? analog.override_state
                : GameAPI::Analogs::getState(RI_MGR, analog);

        ImGui::PushID(index);
        ImGui::VSliderFloat(
            "##v",
            slider_size,
            &new_state,
            0.f, 1.f,
            "Knob",
            ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
        if (new_state != analog.override_state) {
            analog.override_state = new_state;
            analog.override_enabled = true;
        }
    }
}
