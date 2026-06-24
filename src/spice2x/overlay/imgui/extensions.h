#pragma once

#include <string>
#include "external/imgui/imgui.h"

namespace ImGui {

    constexpr ImGuiHoveredFlags TOOLTIP_FLAGS =
        (ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);

    void HelpTooltip(const char* desc);
    void HelpMarker(const char* desc);
    void WarnTooltip(const char* desc, const char* warn);
    void WarnMarker(const char* desc, const char* warn);
    void DummyMarker();

    void Knob(float fraction, float size, float thickness = 2.f,
            float pos_x = -1.f, float pos_y = -1.f);

    void TextTruncated(const std::string& p_text, float p_truncated_width);
    bool DeleteButton(const std::string& tooltip);
    bool ClearButton(const std::string& tooltip);
    void HighlightTableRowOnHover();

    // a Button with the given base fill color; the hovered/active shades are
    // derived by brightening the base. size defaults to auto (fit the label).
    bool ColoredButton(const char* label, const ImVec4& base,
            const ImVec2& size = ImVec2(0, 0));

    // Config tab bar with extra label padding and uniform, centered tab widths.
    // Wrap items in BeginPaddedTabItem between BeginPaddedTabBar/EndTabBar.
    bool BeginPaddedTabBar(const char* str_id, ImGuiTabBarFlags flags = 0);
    bool BeginPaddedTabItem(const char* label);
}
