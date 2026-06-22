#include "extensions.h"
#include <algorithm>
#include <cmath>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_internal.h"
#include "overlay/overlay.h"


namespace ImGui {

    const auto fg = ImVec4(0.910f, 0.914f, 0.922f, 1.0f);
    const auto bg = ImVec4(0.192f, 0.212f, 0.220f, 1.0f);

    // FramePadding shared by the config tab bar and its items so the bar height
    // matches the padded tabs; gives the labels a bit more breathing room.
    static ImVec2 PaddedTabFramePadding() {
        const ImVec2 base = ImGui::GetStyle().FramePadding;
        return ImVec2(
                base.x + overlay::apply_scaling(10.0f),
                base.y + overlay::apply_scaling(1.0f));
    }

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
        // "(?)" and "(!)" render to the same width, so calc it so the spacing
        // tracks the current font size/scale instead of a fixed pixel count.
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize("(?)").x, 0));
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
            ImGui::HelpTooltip(tooltip.c_str());
        }
        ImGui::PopID();
        return clicked;
    }

    bool ColoredButton(const char* label, const ImVec4& base, const ImVec2& size) {
        const auto brighten = [](const ImVec4& c, float d) {
            return ImVec4((std::min)(c.x + d, 1.0f), (std::min)(c.y + d, 1.0f),
                          (std::min)(c.z + d, 1.0f), c.w);
        };
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(base, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(base, 0.22f));
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
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
            ImGui::HelpTooltip(tooltip.c_str());
        }
        ImGui::PopID();
        return clicked;
    }

    bool BeginPaddedTabItem(const char* label) {
        // fixed uniform label width (scaled for DPI) so all tabs are equally sized;
        // wide enough to fit the longest label ("Controller")
        const float uniform_width = overlay::apply_scaling(70.0f);

        // ImGui renders tab labels left-aligned, so a forced width would leave short
        // labels hugging the left edge. Instead we pad the label with equal leading/
        // trailing spaces to reach a uniform width, which both keeps all tabs the same
        // size and centers the text. A stable "##" id suffix keeps each tab's identity
        // independent of the padding.
        const float space_w = ImGui::CalcTextSize(" ").x;
        const float label_w = ImGui::CalcTextSize(label).x;
        const int pad = (space_w > 0.0f)
                ? (int) ((uniform_width - label_w) * 0.5f / space_w)
                : 0;
        const std::string padded = (pad > 0)
                ? std::string(pad, ' ') + label + std::string(pad, ' ') + "##" + label
                : std::string(label);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, PaddedTabFramePadding());
        const bool open = ImGui::BeginTabItem(padded.c_str());
        ImGui::PopStyleVar();
        return open;
    }

    bool BeginPaddedTabBar(const char* str_id, ImGuiTabBarFlags flags) {
        // push the same padding used by the tab items so the bar height matches
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, PaddedTabFramePadding());
        const bool open = ImGui::BeginTabBar(str_id, flags);
        ImGui::PopStyleVar();
        return open;
    }

    void HighlightTableRowOnHover() {
        // hit-test the row rect directly so the row layout and height are untouched
        ImGuiTable *table = ImGui::GetCurrentTable();
        if (table == nullptr) {
            return;
        }
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(
                ImVec2(table->WorkRect.Min.x, table->RowPosY1),
                ImVec2(table->WorkRect.Max.x, table->RowPosY2),
                false)) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(200, 200, 200, 24));
        }
    }
}
