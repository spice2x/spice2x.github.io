#include <games/io.h>
#include "keypad.h"

#include "avs/game.h"
#include "games/iidx/iidx.h"
#include "misc/eamuse.h"
#include "util/logging.h"
#include "overlay/imgui/extensions.h"

namespace overlay::windows {

    Keypad::Keypad(SpiceOverlay *overlay, size_t unit) : Window(overlay), unit(unit) {
        this->title = "Keypad P" + to_string(unit + 1);
        this->flags = ImGuiWindowFlags_NoResize
                    | ImGuiWindowFlags_NoCollapse
                    | ImGuiWindowFlags_AlwaysAutoResize;

        switch (this->unit) {
            case 0: {
                this->toggle_button = games::OverlayButtons::ToggleVirtualKeypadP1;
                this->init_pos = ImVec2(
                        overlay::apply_scaling(26),
                        ImGui::GetIO().DisplaySize.y - overlay::apply_scaling(264));
                break;
            }
            case 1: {
                this->toggle_button = games::OverlayButtons::ToggleVirtualKeypadP2;
                this->init_pos = ImVec2(
                        ImGui::GetIO().DisplaySize.x - overlay::apply_scaling(220),
                        ImGui::GetIO().DisplaySize.y - overlay::apply_scaling(264));
                break;
            }
        }
    }

    Keypad::~Keypad() {

        // reset overrides
        eamuse_set_keypad_overrides_overlay(this->unit, 0);
    }

    void Keypad::build_content() {
        if (avs::game::is_model("LDJ") && games::iidx::TDJ_MODE) {
            build_tdj_keypad();
        } else {
            build_keypad();
        }
    }

    void Keypad::build_keypad() {

        // buttons
        static const struct {
            const char *text;
            int flag;
        } BUTTONS[] = {
            { "7", 1 << EAM_IO_KEYPAD_7 },
            { "8", 1 << EAM_IO_KEYPAD_8 },
            { "9", 1 << EAM_IO_KEYPAD_9 },
            { "4", 1 << EAM_IO_KEYPAD_4 },
            { "5", 1 << EAM_IO_KEYPAD_5 },
            { "6", 1 << EAM_IO_KEYPAD_6 },
            { "1", 1 << EAM_IO_KEYPAD_1 },
            { "2", 1 << EAM_IO_KEYPAD_2 },
            { "3", 1 << EAM_IO_KEYPAD_3 },
            { "0", 1 << EAM_IO_KEYPAD_0 },
            { "00", 1 << EAM_IO_KEYPAD_00 },
            { ".", 1 << EAM_IO_KEYPAD_DECIMAL },
            { "Insert Card", 1 << EAM_IO_INSERT },
        };

        // reset overrides
        eamuse_set_keypad_overrides_overlay(this->unit, 0);

        // build grid
        for (size_t i = 0; i < std::size(BUTTONS); i++) {
            auto &button = BUTTONS[i];

            // push id and alignment
            ImGui::PushID(4096 + i);
            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));

            // add selectable (fill last line)
            if (i == std::size(BUTTONS) - 1) {
                ImGui::Selectable(
                    button.text,
                    false,
                    0,
                    overlay::apply_scaling_to_vector(ImVec2(112, 32)));
            } else {
                ImGui::Selectable(
                    button.text,
                    false,
                    0,
                    overlay::apply_scaling_to_vector(ImVec2(32, 32)));
            }

            // mouse down handler
            if (ImGui::IsItemActive()) {
                eamuse_set_keypad_overrides_overlay(this->unit, button.flag);
            }

            // pop id and alignment
            ImGui::PopStyleVar();
            ImGui::PopID();

            // line join
            if ((i % 3) < 2) {
                ImGui::SameLine();
            }
        }
    }

    void Keypad::build_tdj_keypad() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Keypad disabled in TDJ!\nUse subscreen overlay.");
        ImGui::SameLine();
        ImGui::WarnMarker(
            nullptr,
            "Lightning Model cabinets (TDJ) do not have any keypads; they use the subscreen.\n\n"
            "Fullscreen mode: bind a key in Overlay tab, and press it in game to show the subscreen, "
            "then use your mouse to click. Page Up button is the default binding.\n\n"
            "Windowed mode: look for the second window in the taskbar (or ALT+TAB).\n\n"
            "Windowed mode with -iidxnosub: bring up the subscreen overlay (default Page Up).\n\n"
            );

        ImGui::Spacing();

        if (ImGui::Button("Insert Card")) {
            eamuse_set_keypad_overrides_overlay(this->unit, 1 << EAM_IO_INSERT);
        } else {
            eamuse_set_keypad_overrides_overlay(this->unit, 0);
        }
    }
}
