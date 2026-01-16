#include <iomanip>
#include <sstream>
#include "external/fmt/include/fmt/chrono.h"
#include "fps.h"

namespace overlay::windows {

    FPS::FPS(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Stats";
        this->flags = ImGuiWindowFlags_NoTitleBar
                      | ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_NoCollapse
                      | ImGuiWindowFlags_AlwaysAutoResize
                      | ImGuiWindowFlags_NoFocusOnAppearing
                      | ImGuiWindowFlags_NoNavFocus
                      | ImGuiWindowFlags_NoNavInputs
                      | ImGuiWindowFlags_NoDocking;
        this->bg_alpha = 0.5f;
        this->start_time =
            std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    }

    void FPS::calculate_initial_window() {
        // width is 114x82 px with window decoration, 98x47 for the content
        int pos_x =
            overlay::FPS_SHOULD_FLIP ?
            overlay::apply_scaling(8) :
            ImGui::GetIO().DisplaySize.x - overlay::apply_scaling(122);
        this->init_pos =  ImVec2(pos_x, overlay::apply_scaling(8));
    }

    void FPS::build_content() {

        // frame timers
        ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("FPS:  %.1f", io.Framerate);
        // ImGui::Text("FT: %.2fms", 1000 / io.Framerate);

        const auto now = std::chrono::system_clock::now();
        const auto now_s = std::chrono::floor<std::chrono::seconds>(now);

        // current time
        {
            const std::time_t tt = std::chrono::system_clock::to_time_t(now_s);
            std::tm local_tm{};
            localtime_s(&local_tm, &tt);
            ImGui::TextUnformatted(fmt::format("Time: {:%H:%M:%S}", local_tm).c_str());
        }

        // elapsed time
        {
            const auto uptime = now_s - this->start_time;
            ImGui::TextUnformatted(
                fmt::format("Up:   {:%H:%M:%S}",
                    std::chrono::floor<std::chrono::seconds>(uptime)).c_str());
        }
    }
}
