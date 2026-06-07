#include <algorithm>
#include "external/fmt/include/fmt/chrono.h"
#include "fps.h"

namespace overlay::windows {

    // tighter internal cell padding than the imgui default (4, 2)
    static const ImVec2 FPS_CELL_PADDING(4.0f, 1.0f);

    // tighter window padding than the imgui default (8, 8)
    static const ImVec2 FPS_WINDOW_PADDING(6.0f, 4.0f);

    FPS::FPS(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Stats";
        this->flags = ImGuiWindowFlags_NoTitleBar
                      | ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_NoCollapse
                      | ImGuiWindowFlags_NoFocusOnAppearing
                      | ImGuiWindowFlags_NoBringToFrontOnFocus
                      | ImGuiWindowFlags_NoNavFocus
                      | ImGuiWindowFlags_NoNavInputs
                      | ImGuiWindowFlags_NoNav
                      | ImGuiWindowFlags_NoMove
                      | ImGuiWindowFlags_NoInputs
                      | ImGuiWindowFlags_NoDocking;
        this->bg_alpha = 0.5f;
        this->window_padding = FPS_WINDOW_PADDING;
        this->start_time =
            std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    }

    void FPS::calculate_initial_window() {
        // size the window explicitly (no AlwaysAutoResize) so the corner anchoring
        // below is exact; the footprint mirrors the fixed-fit table in build_content()
        const float line_h = ImGui::GetTextLineHeight();
        const int rows = 3;

        // widest label and widest value drive the two fixed-fit columns
        const float label_w = (std::max)(
            ImGui::CalcTextSize("Time").x,
            ImGui::CalcTextSize("Game").x);
        const float value_w = ImGui::CalcTextSize("00:00:00").x;

        const float win_w = label_w + value_w
            + FPS_CELL_PADDING.x * 2
            + FPS_WINDOW_PADDING.x * 2;
        const float win_h = (line_h + FPS_CELL_PADDING.y * 2) * rows
            + FPS_WINDOW_PADDING.y * 2;
        this->init_size = ImVec2(win_w, win_h);

        // bottom-anchored windows use a larger edge margin (matching notification
        // toasts) since they overlap the same on-screen UI; other edges hug closer
        const float edge_margin = overlay::apply_scaling(4);
        const float bottom_margin = overlay::apply_scaling(20);
        const ImVec2 &display = ImGui::GetIO().DisplaySize;

        const bool right =
            overlay::FPS_LOCATION == overlay::FpsLocation::TopRight ||
            overlay::FPS_LOCATION == overlay::FpsLocation::BottomRight;
        const bool bottom =
            overlay::FPS_LOCATION == overlay::FpsLocation::BottomLeft ||
            overlay::FPS_LOCATION == overlay::FpsLocation::BottomRight;

        const float pos_x = right ? display.x - win_w - edge_margin : edge_margin;
        const float pos_y = bottom ? display.y - win_h - bottom_margin : edge_margin;
        this->init_pos = ImVec2(pos_x, pos_y);
    }

    void FPS::build_content() {

        ImGuiIO &io = ImGui::GetIO();

        const auto now = std::chrono::system_clock::now();
        const auto now_s = std::chrono::floor<std::chrono::seconds>(now);

        const std::time_t tt = std::chrono::system_clock::to_time_t(now_s);
        std::tm local_tm{};
        localtime_s(&local_tm, &tt);

        const auto uptime = now_s - this->start_time;

        // right-align a label within the current cell so the label column reads
        // flush against the value column instead of looking ragged. the label is
        // only slightly dimmer than normal text (not the much darker "disabled" tone)
        const ImGuiStyle &style = ImGui::GetStyle();
        ImVec4 label_col = style.Colors[ImGuiCol_Text];
        label_col.w *= 0.7f;
        const auto label = [&label_col](const char *text) {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float text_w = ImGui::CalcTextSize(text).x;
            if (avail > text_w) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - text_w));
            }
            ImGui::TextColored(label_col, "%s", text);
        };

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, FPS_CELL_PADDING);
        if (ImGui::BeginTable("##fps_stats", 2, ImGuiTableFlags_SizingFixedFit)) {

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            label("FPS");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f", io.Framerate);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            label("Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(fmt::format("{:%H:%M:%S}", local_tm).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            label("Game");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(
                fmt::format("{:%H:%M:%S}",
                    std::chrono::floor<std::chrono::seconds>(uptime)).c_str());

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
}
