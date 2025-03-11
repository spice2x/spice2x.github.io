#include "log.h"
#include "util/utils.h"
#include "util/fileutils.h"
#include "games/io.h"


namespace overlay::windows {

    Log::Log(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Log";
        this->toggle_button = games::OverlayButtons::ToggleLog;
        this->init_size = ImVec2(
                ImGui::GetIO().DisplaySize.x * 0.8f,
                ImGui::GetIO().DisplaySize.y * 0.8f);
        this->size_min = ImVec2(250, 200);
        this->init_pos = ImVec2(
                ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2);

        // read existing contents from file
        if (LOG_FILE_PATH.length() > 0) {
            auto contents = fileutils::text_read(LOG_FILE_PATH);
            if (contents.length() > 0) {
                this->log_hook(this, contents, logger::Style::DEFAULT, contents);
            }
        }

        // add log hook
        logger::hook_add(&log_hook, this);
    }

    Log::~Log() {

        // remove log hook
        logger::hook_remove(&log_hook, this);
    }

    void Log::clear() {

        // lock and clear the data vector
        std::lock_guard<std::mutex> lock(this->log_data_m);
        this->log_data.clear();
    }

    void Log::build_content() {

        // clear button
        if (ImGui::Button("Clear")) {
            this->clear();
        }

        // autoscroll option
        ImGui::SameLine();
        ImGui::Checkbox("Autoscroll", &this->autoscroll);

        // filter
        ImGui::SameLine();
        this->filter.Draw("Filter", -50.f);

        // log area
        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        // iterate log data
        this->log_data_m.lock();
        for (auto &data : log_data) {

            // ignore empty lines and check filter
            if (data.first != "\r\n" && this->filter.PassFilter(data.first.c_str())) {

                // decide on color
                ImVec4 col(1.f, 1.f, 1.f, 1.f);
                switch (data.second) {
                    case logger::GREY:
                        col = ImVec4(0.6f, 0.6f, 0.6f, 1.f);
                        break;
                    case logger::YELLOW:
                        col = ImVec4(1.f, 1.f, 0.f, 1.f);
                        break;
                    case logger::RED:
                        col = ImVec4(1.f, 0.f, 0.f, 1.f);
                        break;
                    case logger::DEFAULT:
                    default:
                        break;
                }

                // draw text
                ImGui::TextColored(col, "%s", data.first.c_str());
            }
        }
        this->log_data_m.unlock();

        // automatic scrolling to bottom
        if (scroll_to_bottom) {
            scroll_to_bottom = false;
            ImGui::SetScrollHereY(1.f);
        }

        // end log area
        ImGui::EndChild();
    }

    bool Log::log_hook(void *user, const std::string &data, logger::Style style, std::string &out) {

        // get reference from user pointer
        auto This = reinterpret_cast<Log *>(user);

        // copy log data
        This->log_data_m.lock();
        This->log_data.emplace_back(data, style);
        This->log_data_m.unlock();

        // autoscroll
        if (This->autoscroll) {
            This->scroll_to_bottom = true;
        }

        // don't replace log data
        return false;
    }
}
