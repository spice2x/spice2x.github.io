#pragma once

namespace ImGui {

    void HelpMarker(const char* desc);
    void WarnMarker(const char* desc, const char* warn);
    void DummyMarker();
    void Knob(float fraction, float size, float thickness = 2.f,
            float pos_x = -1.f, float pos_y = -1.f);
}
