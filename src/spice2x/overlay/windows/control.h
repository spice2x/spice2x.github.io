#pragma once

#include "overlay/window.h"

namespace overlay::windows {

    class Control : public Window {
    public:

        Control(SpiceOverlay *overlay);
        ~Control() override;

        void build_content() override;

    private:

        // state
        char card_input[17] {};

        // other windows
        bool demo_open = false;
        bool metrics_open = false;
        std::vector<float> cpu_values;

        // memory editor
        bool memory_editor_open = false;
        int memory_editor_selection = -1;
        std::vector<std::pair<std::string, HMODULE>> memory_editor_modules;
        std::vector<const char*> memory_editor_names;
        ImGuiTextFilter memory_editor_filter;
        
        // pane views
        void top_row_buttons();
        void img_gui_view();
        void avs_info_view();
        void acio_view();
        void cpu_view();
        void graphics_view();
        void buttons_view();
        void analogs_view();
        void lights_view();
        void cards_view();
        void coin_view();
        void control_view();
        void api_view();
        void raw_input_view();
        void touch_view();
        void lcd_view();
        void about_view();
        void ddr_timing_view();
        void iidx_effectors_view();
    };
} 
