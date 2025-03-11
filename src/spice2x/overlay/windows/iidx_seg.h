#pragma once

#include <optional>
#include "overlay/window.h"

namespace overlay::windows {

    extern uint32_t IIDX_SEGMENT_FONT_SIZE;
    extern std::optional<uint32_t> IIDX_SEGMENT_FONT_COLOR;
    extern std::string IIDX_SEGMENT_LOCATION;

    class IIDXSegmentDisplay : public Window {
    public:
        IIDXSegmentDisplay(SpiceOverlay *overlay);
        void calculate_initial_window() override;
        void build_content() override;
    private:
        ImVec4 color;
        void draw_ticker(char *ticker_string);
    };
}
