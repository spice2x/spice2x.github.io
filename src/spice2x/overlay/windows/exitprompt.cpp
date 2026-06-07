#include "exitprompt.h"
#include "avs/game.h"
#include "build/defs.h"
#include "misc/eamuse.h"
#include "util/logging.h"
#include "games/iidx/iidx.h"
#include "games/gitadora/gitadora.h"
#include "games/popn/popn.h"

namespace overlay::windows {

    ExitPrompt::ExitPrompt(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "spice2x (" + to_string(VERSION_STRING_CFG) + ")";
        this->init_size = ImVec2(
            (ImGui::GetFontSize() * 14) + (ImGui::GetStyle().ItemSpacing.x * 2),
            overlay::apply_scaling(120));
        this->init_pos = ImVec2(
                    ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                    overlay::apply_scaling(10));

        this->flags = ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_NoCollapse
                      | ImGuiWindowFlags_AlwaysAutoResize
                      | ImGuiWindowFlags_NoDocking
                      | ImGuiWindowFlags_NoMove;

        // the menu renders itself as a popup, not a normal overlay window
        this->draws_window = false;
    }

    void ExitPrompt::update() {
        Window::update();

        // allow the popup to be reopened the next time the menu is shown
        if (!this->active) {
            this->popup_opened = false;
        }
    }

    void ExitPrompt::build_button(
        Window *window, std::string label, const ImVec2 &size, NextItem next) {

        if (window == nullptr) {
            return;
        }

        if (ImGui::Button(label.c_str(), size)) {
            window->toggle_active();

            // raise to the top when the user toggled it visible from the menu
            if (window->get_active()) {
                window->bring_to_front();
            }
        }
        if (next == NextItem::NEW_LINE) {
            ImGui::Spacing();
        } else {
            ImGui::SameLine();
        }
    }

    void ExitPrompt::build_content() {
        // use ### so the visible label is the full title (with version) while the
        // popup keeps a stable identifier regardless of the title text
        const std::string popup_id_str = this->title + "###mainmenu";
        const char *popup_id = popup_id_str.c_str();

        // open the popup once when the menu becomes active; popup_opened is reset
        // in update() while inactive so it reopens the next time it is shown
        if (!this->popup_opened) {
            ImGui::OpenPopup(popup_id);
            this->popup_opened = true;
        }

        // position at the top center of the screen
        const ImVec2 menu_pos(
            ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
            overlay::apply_scaling(10));
        ImGui::SetNextWindowPos(menu_pos, ImGuiCond_Appearing);

        bool open = true;
        if (!ImGui::BeginPopupModal(popup_id, &open, this->flags)) {
            // dismissed via escape - close the menu
            this->set_active(false);
            return;
        }

        const ImVec2 size(ImGui::GetFontSize() * 14, ImGui::GetFontSize() * 1.9f);
        const ImVec2 size_half(
            (size.x - ImGui::GetStyle().ItemSpacing.x) / 2,
            ImGui::GetFontSize() * 1.9f);
        const ImVec2 size_third(
            (size.x - (ImGui::GetStyle().ItemSpacing.x * 2)) / 3,
            ImGui::GetFontSize() * 2.5f);

        build_button(this->overlay->window_config, "Options", size, NextItem::NEW_LINE);

        std::string sub = "Show Subscreen";
        if (avs::game::is_model("LDJ")) {
            if (games::iidx::TDJ_MODE) {
                sub = "Show TDJ Subscreen";
            } else {
                sub = "Show LDJ LED Ticker";
            }
        } else if (avs::game::is_model("KFC")) {
            sub = "Show Valkyrie Subscreen";
        } else if (avs::game::is_model("REC")) {
            sub = "Show DRS Dance Floor";
        } else if (games::gitadora::is_arena_model()) {
            sub = "Show GITADORA Subscreen";
        } else if (games::popn::is_pikapika_model()) {
            sub = "Show Pop'n Subscreen";
        }

        build_button(this->overlay->window_sub, sub, size, NextItem::NEW_LINE);

        ImGui::TextDisabled("Graphics");
        build_button(this->overlay->window_camera, "Camera control", size, NextItem::NEW_LINE);

        build_button(this->overlay->window_fps.get(), "FPS", size_half, NextItem::SAME_LINE);
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
                ImGuiWindowFlags_NoDocking |
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

        ImGui::EndPopup();

        // title-bar X was clicked - close the menu
        if (!open) {
            this->set_active(false);
        }
    }
}
