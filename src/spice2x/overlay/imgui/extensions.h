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
}
