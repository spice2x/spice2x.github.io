#include "eadev.h"

#include "avs/automap.h"
#include "util/fileutils.h"
#include "overlay/imgui/extensions.h"


namespace overlay::windows {

    EADevWindow::EADevWindow(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "EA-Dev";
        this->init_size = ImVec2(
                ImGui::GetIO().DisplaySize.x * 0.8f,
                ImGui::GetIO().DisplaySize.y * 0.8f);
        this->size_min = ImVec2(250, 200);
        this->init_pos = ImVec2(
                ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2);
        this->active = true;

        // read existing automap contents from file
        if (avs::automap::DUMP_FILENAME.length() > 0) {
            auto contents = fileutils::text_read(avs::automap::DUMP_FILENAME);
            if (contents.length() > 0) {
                this->automap_hook(this, contents.c_str());
            }
        }

        // add hook for receiving automap messages
        avs::automap::hook_add(automap_hook, this);
    }

    EADevWindow::~EADevWindow() {
        avs::automap::hook_remove(automap_hook, this);
    }

    void EADevWindow::build_content() {

        // automap
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Automap")) {

            // enable checkbox
            if (ImGui::Checkbox("Enabled", &avs::automap::ENABLED)) {
                if (avs::automap::ENABLED) {
                    avs::automap::enable();
                } else {
                    avs::automap::disable();
                }
            }
            ImGui::SameLine();
            ImGui::HelpMarker("Enable this module.");

            // dump checkbox
            ImGui::Checkbox("Dump", &avs::automap::DUMP);
            if (avs::automap::DUMP_FILENAME.length() > 0) {
                ImGui::SameLine();
                ImGui::Text("- %s", avs::automap::DUMP_FILENAME.c_str());
            }
            ImGui::SameLine();
            ImGui::HelpMarker("Dump all destroyed props to file.");

            // json checkbox
            ImGui::Checkbox("JSON", &avs::automap::JSON);
            ImGui::SameLine();
            ImGui::HelpMarker("Output in JSON instead of XML.");

            // patch checkbox
            ImGui::Checkbox("Patch", &avs::automap::PATCH);
            ImGui::SameLine();
            ImGui::HelpMarker("Try to dynamically add all non-existing nodes which are being accessed. (WIP)");

            // network checkbox
            ImGui::Checkbox("Network Only", &avs::automap::RESTRICT_NETWORK);
            ImGui::SameLine();
            ImGui::HelpMarker("Restrict functionality to calls/responses.");

            // autoscroll checkbox
            ImGui::Checkbox("Auto-Scroll", &this->automap_autoscroll);
            ImGui::SameLine();
            ImGui::HelpMarker("Automatically scroll to bottom.");

            // clear button
            if (!this->automap_data.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    this->automap_data.clear();
                }
            }

            // log view
            ImGui::Separator();
            ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t i = 0; i < automap_data.size(); i++) {
                ImGui::PushStyleColor(ImGuiCol_Text, (i % 2) == 0
                ? ImVec4(1.0f, 0.7f, 0.7f, 1.f)
                : ImVec4(0.7f, 1.0f, 0.7f, 1.f));
                ImGui::TextUnformatted(automap_data[i].c_str());
                ImGui::PopStyleColor();
            }
            if (this->automap_scroll_to_bottom) {
                this->automap_scroll_to_bottom = false;
                ImGui::SetScrollHereY(1.f);
            }
            ImGui::EndChild();
        }
    }

    void EADevWindow::automap_hook(void *user, const char *data) {
        auto This = (EADevWindow*) user;
        This->automap_data.emplace_back(std::string(data));
        if (This->automap_autoscroll) {
            This->automap_scroll_to_bottom = true;
        }
    }
}
