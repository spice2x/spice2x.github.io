#pragma once

#include "overlay/window.h"
#include "cfg/button.h"

namespace overlay::windows {

    class IOPanel : public Window {
    public:
        IOPanel(SpiceOverlay *overlay);
        void build_content() override;

    protected:
        virtual void build_io_panel();
        void build_button(
            const char *label,
            const ImVec2 &size,
            Button *button,
            Button *button_alt = nullptr,
            Light *light = nullptr);

        float get_suggested_height();

    private:
        void find_buttons();
        void build_operator_menu();

        bool operator_unlocked = false;
        Button *test_button = nullptr;
        Button *service_button = nullptr;
    };
}
