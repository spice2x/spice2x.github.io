#include "iopanel.h"
#include "cfg/api.h"
#include "launcher/launcher.h"
#include "games/io.h"
#include "misc/eamuse.h"
#include "overlay/imgui/extensions.h"

namespace overlay::windows {

    IOPanel::IOPanel(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Operator";
        this->flags = ImGuiWindowFlags_AlwaysAutoResize;

        this->init_size = ImVec2(
            overlay::apply_scaling(80),
            ImGui::GetFrameHeight() +
                ImGui::GetStyle().WindowPadding.y * 2 +
                ImGui::GetFrameHeightWithSpacing() * 3 +
                ImGui::GetFrameHeight()); // title + windows padding + 4 buttons

        this->init_pos =ImVec2(10, ImGui::GetIO().DisplaySize.y - this->init_size.y - 10);
        this->toggle_button = games::OverlayButtons::ToggleIOPanel;
        this->find_buttons();
    }

    void IOPanel::find_buttons() {
        const auto buttons = games::get_buttons(eamuse_get_game());
        for (auto &button : *buttons) {
            // since the array indices are different for each game, use string match instead
            if (button.getName() == "Service") {
                this->service_button = &button;
            } else if (button.getName() == "Test") {
                this->test_button = &button;
            }
        }
    }

    float IOPanel::get_suggested_height() {
        // height of 4 buttons stacked vertically
        // (coin, unlock, service, test, plus spacing between them)
        return ImGui::GetFrameHeightWithSpacing() * 3 + ImGui::GetFrameHeight();
    }

    void IOPanel::build_content() {
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.1f, 0.1f, 1.f));
        ImGui::PushID(this);

        ImGui::BeginGroup();
        this->build_operator_menu();
        ImGui::EndGroup();

        ImGui::SameLine();
        this->build_io_panel();

        ImGui::PopID();
        ImGui::PopStyleColor();
    }

    void IOPanel::build_io_panel() {}

    void IOPanel::build_operator_menu() {
        const ImVec2 wide = overlay::apply_scaling_to_vector(60, 0);
        const float spacing_x = ImGui::GetStyle().ItemSpacing.y;
        const ImVec2 tall(
            ImGui::GetFrameHeight(),
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight());

        if (ImGui::Button("COIN", ImVec2(wide.x + spacing_x + tall.x, wide.y))) {
            eamuse_coin_add();
        }

        ImGui::Checkbox("unlock", &operator_unlocked);
        ImGui::BeginDisabled(!operator_unlocked);

        this->build_button("SERVICE", wide, this->service_button);
        this->build_button("TEST", wide, this->test_button);

        // service + test button (for test menu navigation)
        ImGui::SameLine(0, spacing_x);
        ImGui::BeginGroup();
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetFrameHeightWithSpacing());
            this->build_button("+", tall, this->test_button, this->service_button);
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                ImGui::HelpTooltip("SERVICE + TEST");
            }
        }
        ImGui::EndGroup();

        ImGui::EndDisabled();
    }

    void IOPanel::build_button(
        const char *label, const ImVec2 &size, Button *button, Button *button_alt, Light *light) {

        ImGui::BeginDisabled(button == nullptr);

        int pushed = 0;
        if (light && 0.5f < GameAPI::Lights::readLight(RI_MGR, *light)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.15f, 0.00f, 0.9f));
            pushed += 1;
        }

        ImGui::Button(label, size);

        if (ImGui::IsItemActivated()) {
            button->override_state = GameAPI::Buttons::BUTTON_PRESSED;
            button->override_velocity = 1.f;
            button->override_enabled = true;
            if (button_alt) {
                button_alt->override_state = GameAPI::Buttons::BUTTON_PRESSED;
                button_alt->override_velocity = 1.f;
                button_alt->override_enabled = true;
            }
        } else if (ImGui::IsItemDeactivated()) {
            button->override_enabled = false;
            if (button_alt) {
                button_alt->override_enabled = false;
            }
        }

        ImGui::PopStyleColor(pushed);
        ImGui::EndDisabled();
    }
}
