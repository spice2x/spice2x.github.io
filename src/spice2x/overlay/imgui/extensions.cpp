#include "extensions.h"
#include <cmath>

#include "external/imgui/imgui.h"


namespace ImGui {

    const auto fg = ImVec4(0.910f, 0.914f, 0.922f, 1.0f);
    const auto bg = ImVec4(0.192f, 0.212f, 0.220f, 1.0f);

    void HelpTooltip(const char* desc) {
        ImGui::PushStyleColor(ImGuiCol_Border, bg);
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, bg);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Text, fg);

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();

        ImGui::PopStyleColor(4);
    }

    void HelpMarker(const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(TOOLTIP_FLAGS)) {
            HelpTooltip(desc);
        }
    }

    void WarnTooltip(const char* desc, const char* warn) {
        ImGui::PushStyleColor(ImGuiCol_Border, bg);
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, bg);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Text, fg);

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        if (desc && desc[0]) {
            ImGui::TextUnformatted(desc);
            ImGui::TextUnformatted("");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));
        if (warn && warn[0]) {
            ImGui::TextUnformatted("WARNING:");
            ImGui::TextUnformatted(warn);
        }
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();

        ImGui::PopStyleColor(4);
    }

    void WarnMarker(const char* desc, const char* warn) {
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1.f, 1.f, 0.f, 1.f));
        ImGui::TextDisabled("(!)");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(TOOLTIP_FLAGS)) {
            WarnTooltip(desc, warn);
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

    std::string TruncateText(const std::string& p_text, float p_truncated_width, bool &truncated) {
        std::string truncated_text = p_text;

        const float text_width =
                ImGui::CalcTextSize(p_text.c_str(), nullptr, true).x;

        if (text_width > p_truncated_width) {
            truncated = true;
            constexpr const char* ELLIPSIS = " ...";
            const float ellipsis_size = ImGui::CalcTextSize(ELLIPSIS).x;

            int visible_chars = 0;
            for (size_t i = 0; i < p_text.size(); i++) {
                const float current_width = ImGui::CalcTextSize(
                        p_text.substr(0, i).c_str(), nullptr, true)
                                                    .x;
                if (current_width + ellipsis_size > p_truncated_width) {
                    break;
                }

                visible_chars = i;
            }

            truncated_text = (p_text.substr(0, visible_chars) + ELLIPSIS).c_str();
        }

        return truncated_text;
    }

    void TextTruncated(const std::string& p_text, float p_truncated_width) {
        if (p_text.empty()) {
            ImGui::TextDisabled("-");
            return;
        }
        bool truncated = false;
        ImGui::TextUnformatted(TruncateText(p_text, p_truncated_width, truncated).c_str());
        if (truncated && ImGui::IsItemHovered(TOOLTIP_FLAGS)) {
            ImGui::HelpTooltip(p_text.c_str());
        }
    }

    bool DeleteButton(const std::string& tooltip) {
        ImGui::PushID(tooltip.c_str());
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_Button, 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.f, 2.f));
        bool clicked = ImGui::SmallButton("\u00D7"); // multiplication sign (×)
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (!tooltip.empty() && ImGui::IsItemHovered(TOOLTIP_FLAGS)) {
            ImGui::SameLine();
            ImGui::HelpTooltip(tooltip.c_str());
        }
        ImGui::PopID();
        return clicked;
    }

    bool ClearButton(const std::string& tooltip) {
        ImGui::PushID(tooltip.c_str());
        // same colors as a checkbox
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_FrameBgActive));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.97f, 0.97f, 1.00f));
        bool clicked = ImGui::Button("\u00D7"); // multiplication sign (×)
        ImGui::PopStyleColor(4);
        if (!tooltip.empty() && ImGui::IsItemHovered(TOOLTIP_FLAGS)) {
            ImGui::SameLine();
            ImGui::HelpTooltip(tooltip.c_str());
        }
        ImGui::PopID();
        return clicked;
    }
}
