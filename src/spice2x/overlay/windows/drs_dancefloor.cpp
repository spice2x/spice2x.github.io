#include <map>

#include "drs_dancefloor.h"
#include "games/io.h"
#include "games/drs/drs.h"
#include "util/logging.h"

namespace overlay::windows {

    DRSDanceFloorDisplay::DRSDanceFloorDisplay(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "DANCERUSH Floor";
        this->toggle_button = games::OverlayButtons::ToggleSubScreen;
        this->remove_window_padding = true;

        this->size_max = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        this->size_min = ImVec2(100, 128 + ImGui::GetFrameHeight());
        this->init_size = size_min;

        this->resize_callback = this->keep_aspect_ratio;

        this->flags = ImGuiWindowFlags_NoScrollbar
                      | ImGuiWindowFlags_NoNavFocus
                      | ImGuiWindowFlags_NoNavInputs
                      | ImGuiWindowFlags_NoDocking
                      | ImGuiWindowFlags_NoBackground;
    }

    void DRSDanceFloorDisplay::calculate_initial_window() {
        this->init_size.x = 360;
        this->init_size.y = this->init_size.x * DRS_TAPELED_ROWS / DRS_TAPELED_COLS;

        // horizontal right
        this->init_pos.x = ImGui::GetIO().DisplaySize.x - this->init_size.x;
        // vertical center
        this->init_pos.y = ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2;
    }

    void DRSDanceFloorDisplay::build_content() {
        const auto draw_list = ImGui::GetWindowDrawList();
        const auto canvas_pos = ImGui::GetCursorScreenPos();
        const auto canvas_size = ImGui::GetContentRegionAvail();

        const float off_x = canvas_pos.x;
        const float off_y = canvas_pos.y;
        const float scale = std::min(canvas_size.x, canvas_size.y) / DRS_TAPELED_COLS;

        for (int x = 0; x < DRS_TAPELED_COLS; x++) {
            for (int y = 0; y < DRS_TAPELED_ROWS; y++) {
                auto &led = games::drs::DRS_TAPELED[x + y * DRS_TAPELED_COLS];

                ImColor color(
                    ((uint8_t)led[0]) / ((float)DRS_TAPELED_MAX_VAL),
                    ((uint8_t)led[1]) / ((float)DRS_TAPELED_MAX_VAL),
                    ((uint8_t)led[2]) / ((float)DRS_TAPELED_MAX_VAL));

                // if (x == 0 && y == 0) {
                //     log_info("drs", "color: {} {} {}", (uint8_t)led[0], (uint8_t)led[1], (uint8_t)led[2]);
                // }

                ImVec2 p1(x * scale + off_x, y * scale + off_y);
                ImVec2 p2((x + 1) * scale + off_x, (y + 1) * scale + off_y);
                draw_list->AddRectFilled(p1, p2, color, 0.f);
            }
        }
    }
}
