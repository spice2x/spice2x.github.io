#include "exitprompt.h"
#include "misc/eamuse.h"
#include "util/logging.h"

namespace overlay::windows {

    ExitPrompt::ExitPrompt(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "spice2x";
        this->init_size = ImVec2(
            (ImGui::GetFontSize() * 14) + (ImGui::GetStyle().ItemSpacing.x * 2),
            120);
        this->init_pos = ImVec2(
                    ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                    10);

        this->flags = ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_NoCollapse
                      | ImGuiWindowFlags_AlwaysAutoResize;
    }

    void ExitPrompt::build_button(
        Window *window, std::string label, const ImVec2 &size, NextItem next, bool is_toggle) {

        if (window == nullptr) {
            return;
        }

        if (ImGui::Button(label.c_str(), size)) {
            if (is_toggle) {
                window->toggle_active();
            } else {
                window->set_active(true);
                this->set_active(false);
            }
        }
        if (next == NextItem::NEW_LINE) {
            ImGui::Spacing();
        } else {
            ImGui::SameLine();
        }
    }

    void ExitPrompt::build_content() {
        const ImVec2 size(ImGui::GetFontSize() * 14, ImGui::GetFontSize() * 1.9f);
        const ImVec2 size_half(
            (size.x - ImGui::GetStyle().ItemSpacing.x) / 2,
            ImGui::GetFontSize() * 1.9f);
        const ImVec2 size_third(
            (size.x - (ImGui::GetStyle().ItemSpacing.x * 2)) / 3,
            ImGui::GetFontSize() * 2.5f);

        if (ImGui::Button("Hide overlay", size)) {
            overlay::OVERLAY->set_active(false);
        }
        ImGui::Spacing();
        build_button(this->overlay->window_config, "Show Config", size, NextItem::NEW_LINE, false);
        build_button(this->overlay->window_sub, "Show Subscreen", size, NextItem::NEW_LINE, false);

        ImGui::TextDisabled("Graphics");
        build_button(this->overlay->window_camera, "Camera control", size, NextItem::NEW_LINE);
        build_button(this->overlay->window_fps, "FPS", size_half, NextItem::SAME_LINE);
        build_button(this->overlay->window_resize, "Resize", size_half, NextItem::NEW_LINE);

        ImGui::TextDisabled("I/O");
        build_button(this->overlay->window_cards, "Card Manager", size, NextItem::NEW_LINE);
        if (this->overlay->window_keypad2 == nullptr) {
            // 1p only
            build_button(this->overlay->window_keypad1, "Keypad", size_half, NextItem::SAME_LINE);
            build_button(this->overlay->window_iopanel, "I/O panel", size_half, NextItem::NEW_LINE);
        } else {
            // 1p and 2p
            build_button(this->overlay->window_keypad1, "Keypad\n  P1", size_third, NextItem::SAME_LINE);
            build_button(this->overlay->window_iopanel, " I/O\npanel", size_third, NextItem::SAME_LINE);
            build_button(this->overlay->window_keypad2, "Keypad\n  P2", size_third, NextItem::NEW_LINE);
        }

        ImGui::TextDisabled("Debug");
        build_button(this->overlay->window_control, "Control", size_half, NextItem::SAME_LINE);
        build_button(this->overlay->window_log, "Log", size_half, NextItem::NEW_LINE);

        ImGui::TextDisabled("Windows audio volume");
        if (ImGui::Button("-", size_third)) {
            INPUT keys = {};
            keys.type = INPUT_KEYBOARD;
            keys.ki.wVk = VK_VOLUME_DOWN;
            SendInput(1, &keys, sizeof(keys));
        }
        ImGui::SameLine();
        if (ImGui::Button("Mute", size_third)) {
            INPUT keys = {};
            keys.type = INPUT_KEYBOARD;
            keys.ki.wVk = VK_VOLUME_MUTE;
            SendInput(1, &keys, sizeof(keys));
        }
        ImGui::SameLine();
        if (ImGui::Button("+", size_third)) {
            INPUT keys = {};
            keys.type = INPUT_KEYBOARD;
            keys.ki.wVk = VK_VOLUME_UP;
            SendInput(1, &keys, sizeof(keys));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // quit
        if (ImGui::Button("Exit Game", size)) {
            ImGui::OpenPopup("spice2x##quitdialog");
        }
        if (ImGui::BeginPopupModal(
                "spice2x##quitdialog",
                nullptr,
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Exit the game now?");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Exit")) {
                log_info("exitprompt", "user chose to quit game...");
                launcher::shutdown();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Spacing();
    }
}
