#include <map>
#include "iopanel_iidx.h"
#include "games/io.h"
#include "games/iidx/iidx.h"
#include "games/iidx/io.h"
#include "misc/eamuse.h"
#include "util/logging.h"

namespace overlay::windows {

    IIDXIOPanel::IIDXIOPanel(SpiceOverlay *overlay) : IOPanel(overlay) {
        this->title = "IIDX IO Panel";
        find_iidx_buttons();
    }

    void IIDXIOPanel::find_iidx_buttons() {
        const auto buttons = games::get_buttons(eamuse_get_game());
        const auto lights = games::get_lights(eamuse_get_game());

        this->start_1p = &(*buttons)[games::iidx::Buttons::P1_Start];
        this->start_1p_light = &(*lights)[games::iidx::Lights::P1_Start];

        this->start_2p = &(*buttons)[games::iidx::Buttons::P2_Start];
        this->start_2p_light = &(*lights)[games::iidx::Lights::P2_Start];

        this->vefx = &(*buttons)[games::iidx::Buttons::VEFX];
        this->vefx_light = &(*lights)[games::iidx::Lights::VEFX];

        this->effect_on = &(*buttons)[games::iidx::Buttons::Effect];
        this->effect_on_light = &(*lights)[games::iidx::Lights::Effect];
    }

    void IIDXIOPanel::build_io_panel() {
        ImGui::Dummy(ImVec2(12, 0));

        ImGui::SameLine();
        this->draw_buttons();

        if (!games::iidx::TDJ_MODE) {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(12, 0));

            ImGui::SameLine();
            this->draw_sliders();
        }
    }

    void IIDXIOPanel::draw_buttons() {
        const auto big_button_size = ImVec2(get_suggested_height(), get_suggested_height());

        // width = same as above,
        // height = 2 rows of buttons
        const auto mid_button_size = ImVec2(
            big_button_size.x,
            ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.y
            );

        // width = half of above,
        // height = 4 rows of buttons
        const auto tall_button_size = ImVec2(
            big_button_size.x / 2,
            big_button_size.y
            );

        this->build_button(
            " 1 P\n"
            "Start",
            big_button_size,
            this->start_1p, nullptr,
            this->start_1p_light);

        ImGui::SameLine();
        ImGui::BeginGroup();
        this->build_button(
            "EFFECT",
            mid_button_size,
            this->effect_on, nullptr,
            this->effect_on_light);
        this->build_button(
            "VEFX",
            mid_button_size,
            this->vefx, nullptr,
            this->vefx_light);
        ImGui::EndGroup();
        
        ImGui::SameLine();
        this->build_button(
            "EFF\n"
            " +\n"
            "VFX",
            tall_button_size,
            this->effect_on,
            this->vefx);

        ImGui::SameLine();
        this->build_button(
            " 2 P\n"
            "Start",
            big_button_size,
            this->start_2p, nullptr,
            this->start_2p_light);
    }

    void IIDXIOPanel::draw_sliders() {
        // effector analog entries
        static const std::map<size_t, const char *> ANALOG_ENTRIES {
            { games::iidx::Analogs::VEFX, "VEFX" },
            { games::iidx::Analogs::LowEQ, "LoEQ" },
            { games::iidx::Analogs::HiEQ, "HiEQ" },
            { games::iidx::Analogs::Filter, "Fltr" },
            { games::iidx::Analogs::PlayVolume, "Vol." },
        };

        const ImVec2 slider_size(32, get_suggested_height());
        const auto reset_button_size = ImVec2(16, get_suggested_height());

        // iterate analogs
        bool overridden = false;
        const auto analogs = games::get_analogs(eamuse_get_game());
        for (auto &[index, name] : ANALOG_ENTRIES) {

            // safety check
            if (index >= analogs->size()) {
                continue;
            }

            // get analog
            auto &analog = (*analogs)[index];
            overridden |= analog.override_enabled;

            // push id and style
            ImGui::PushID((void *) name);

            // vertical slider
            auto new_state = analog.override_enabled ? analog.override_state
                    : GameAPI::Analogs::getState(RI_MGR, analog);
            if (0 < index) {
                ImGui::SameLine();
            }

            ImGui::VSliderFloat(
                "##v",
                slider_size,
                &new_state,
                0.f, 1.f,
                name,
                ImGuiSliderFlags_AlwaysClamp);

            if (new_state != analog.override_state) {
                analog.override_state = new_state;
                analog.override_enabled = true;
            }

            // pop id and style
            ImGui::PopID();
        }

        // reset button
        ImGui::SameLine();
        if (ImGui::Button("r\ne\ns\ne\nt", reset_button_size)) {
            for (auto &[index, name] : ANALOG_ENTRIES) {
                if (index < analogs->size()) {
                    (*analogs)[index].override_enabled = false;
                }
            }
        }
    }
}
