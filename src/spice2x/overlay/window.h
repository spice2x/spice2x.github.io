#pragma once

#include <string>

#include "external/imgui/imgui.h"
#include "external/imgui/misc/cpp/imgui_stdlib.h"
#include "overlay.h"

namespace overlay {

    class Window {
    public:
        virtual ~Window();

        // an opportunity to calculate initial window position and size within the ImGui
        // window context, since it may not be possible in the Window constructor
        virtual void calculate_initial_window() {}

        virtual void build_content() = 0;
        virtual void update();
        virtual void after_render() {};

        void build();
        void toggle_active();
        void set_active(bool active);
        bool get_active();

        // raise this window to the top of the z-order on its next build. only
        // call in response to an explicit user action (keyboard toggle, UI
        // button) - not for auto-show / programmatic shows.
        void bring_to_front();
    protected:

        // state
        SpiceOverlay *overlay;
        bool active = false;
        // set when the window transitions to visible so build() raises it to the
        // top of the z-order on the next frame
        bool request_focus = false;
        std::vector<Window*> children;

        // settings
        bool remove_window_padding = false;
        // custom inner window padding, applied before Begin() (preserving border /
        // rounding) when x >= 0; ignored if remove_window_padding is set
        ImVec2 window_padding = ImVec2(-1, -1);
        bool draws_window = true;
        ImGuiSizeCallback resize_callback = nullptr;
        std::string title = "Title";
        ImGuiWindowFlags flags = 0;
        size_t toggle_button = ~0u;
        bool toggle_button_state = false;

        // init settings
        // calculate_initial_window() runs only once; results feed ImGuiCond_Once
        bool initial_window_calculated = false;
        ImVec2 init_pos = ImVec2(0, 0);
        ImVec2 init_size = ImVec2(0, 0);
        ImVec2 size_min = ImVec2(0, 0);
        ImVec2 size_max = ImVec2(-1, -1);
        float bg_alpha = 0.96f;

        Window(SpiceOverlay *overlay);
    };
}
