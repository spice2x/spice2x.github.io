#include "extensions.h"
#include <cmath>

#include "external/imgui/imgui.h"


namespace ImGui {

    void HelpMarker(const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void WarnMarker(const char* desc, const char* warn) {
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1.f, 1.f, 0.f, 1.f));
        ImGui::TextDisabled("(!)");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            if (desc) {
                ImGui::TextUnformatted(desc);
                ImGui::TextUnformatted("");
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));
            if (warn) {
                ImGui::TextUnformatted("WARNING:");
                ImGui::TextUnformatted(warn);
            }
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void DummyMarker() {
        // dummy marker that is the same width as HelpMarker/WarnMarker.
        ImGui::Dummy(ImVec2(22, 0));
    }

    void Knob(float fraction, float size, float thickness, float pos_x, float pos_y) {

        // get values
        auto radius = size * 0.5f;
        auto pos = ImGui::GetCursorScreenPos();
        if (pos_x >= 0) pos.x = pos_x;
        if (pos_y >= 0) pos.y = pos_y;
        auto center = ImVec2(pos.x + radius, pos.y + radius);
        auto draw_list = ImGui::GetWindowDrawList();

        // dummy for spacing knob with other content
        if (pos_x < 0 && pos_y < 0) {
            ImGui::Dummy(ImVec2(size, size));
        }

        // draw knob
        auto angle = (fraction + 0.25f) * (3.141592f * 2);
        draw_list->AddCircleFilled(center, radius, ImGui::GetColorU32(ImGuiCol_FrameBg), 16);
        draw_list->AddLine(center,
                ImVec2(center.x + cosf(angle) * radius, center.y + sinf(angle) * radius),
                ImGui::GetColorU32(ImGuiCol_PlotHistogram),
                thickness);
    }
}
