#include <algorithm>
#include "external/fmt/include/fmt/chrono.h"
#include "fps.h"
#include "obs.h"

namespace overlay::windows {

    // tighter internal cell padding than the imgui default (4, 2)
    static const ImVec2 FPS_CELL_PADDING(4.0f, 1.0f);

    // tighter window padding than the imgui default (8, 8)
    static const ImVec2 FPS_WINDOW_PADDING(6.0f, 4.0f);

    FPS::FPS(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Stats";
        this->flags = ImGuiWindowFlags_NoTitleBar
                      | ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_AlwaysAutoResize
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

    ImVec2 FPS::anchored_pos(const ImVec2 &size) const {
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

        const float pos_x = right ? display.x - size.x - edge_margin : edge_margin;
        const float pos_y = bottom ? display.y - size.y - bottom_margin : edge_margin;
        return ImVec2(pos_x, pos_y);
    }

    void FPS::calculate_initial_window() {
        // first-frame size estimate for the base 3 rows; AlwaysAutoResize handles
        // the exact size (incl. any OBS rows) and build_content re-anchors each frame
        const float line_h = ImGui::GetTextLineHeight();
        const float label_w = (std::max)(
            ImGui::CalcTextSize("Time").x,
            ImGui::CalcTextSize("Game").x);
        const float value_w = ImGui::CalcTextSize("00:00:00").x;

        const float win_w = label_w + value_w
            + FPS_CELL_PADDING.x * 2
            + FPS_WINDOW_PADDING.x * 2;
        const float win_h = (line_h + FPS_CELL_PADDING.y * 2) * 3
            + FPS_WINDOW_PADDING.y * 2;
        this->init_size = ImVec2(win_w, win_h);
        this->init_pos = this->anchored_pos(this->init_size);
    }

    void FPS::build_content() {

        ImGuiIO &io = ImGui::GetIO();

        const auto now = std::chrono::system_clock::now();
        const auto now_s = std::chrono::floor<std::chrono::seconds>(now);

        const std::time_t tt = std::chrono::system_clock::to_time_t(now_s);
        std::tm local_tm{};
        localtime_s(&local_tm, &tt);

        const auto uptime = now_s - this->start_time;

        // OBS status (only adds rows while streaming live or recording/paused)
        OBSStatus obs_status;
        bool show_stream = false;
        bool show_record = false;
        if (auto *obs = static_cast<OBSControl *>(this->overlay->window_obs)) {
            obs_status = obs->get_status();
            show_stream = obs_status.streaming;
            show_record = obs_status.recording;
        }

        // AlwaysAutoResize sizes the window to its content, so adding/removing OBS
        // rows never clips; just re-anchor it to the configured corner each frame
        // using the actual (auto-sized) dimensions
        ImGui::SetWindowPos(this->anchored_pos(ImGui::GetWindowSize()), ImGuiCond_Always);

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

            // OBS rows - only present while live or recording
            const ImVec4 col_red(0.90f, 0.30f, 0.30f, 1.0f);
            const ImVec4 col_yellow(0.95f, 0.80f, 0.30f, 1.0f);
            if (show_stream) {
                const int64_t ms = OBSControl::live_duration_ms(
                    obs_status.stream_duration_ms, obs_status.stream_duration_base_tick, true);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                label("Live");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(col_red, "%s",
                    fmt::format("{:%H:%M:%S}", std::chrono::seconds(ms / 1000)).c_str());
            }
            if (show_record) {
                const int64_t ms = OBSControl::live_duration_ms(
                    obs_status.record_duration_ms, obs_status.record_duration_base_tick,
                    !obs_status.record_paused);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                label("Rec");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(obs_status.record_paused ? col_yellow : col_red, "%s",
                    fmt::format("{:%H:%M:%S}", std::chrono::seconds(ms / 1000)).c_str());
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
}
