#include <iomanip>
#include <sstream>
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
        this->start_time = std::chrono::system_clock::now();
    }

    void FPS::calculate_initial_window() {
        // width is 114x82 px with window decoration, 98x47 for the content
        int pos_x = overlay::FPS_SHOULD_FLIP ? 8 : ImGui::GetIO().DisplaySize.x - 122;
        this->init_pos =  ImVec2(pos_x, 8);
    }

    void FPS::build_content() {

        // frame timers
        ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("FPS:  %.1f", io.Framerate);
        // ImGui::Text("FT: %.2fms", 1000 / io.Framerate);

        auto now = std::chrono::system_clock::now();

        // current time
        {
            auto now_t = std::chrono::system_clock::to_time_t(now);
            static CHAR buf[48];
            std::strftime(buf, sizeof(buf), "Time: %H:%M:%S", std::localtime(&now_t));
            ImGui::Text(buf);
        }

        // elapsed time
        {
            auto d = now - this->start_time;
            const auto h = std::chrono::duration_cast<std::chrono::hours>(d);
            const auto m = std::chrono::duration_cast<std::chrono::minutes>(d - h);
            const auto s = std::chrono::duration_cast<std::chrono::seconds>(d - h - m);
            ImGui::Text("Up:   %02d:%02d:%02d", (int)h.count(), (int)m.count(), (int)s.count());
        }
    }
}
