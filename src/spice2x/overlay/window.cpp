#include "window.h"
#include "cfg/configurator.h"
#include "util/logging.h"
#include "games/io.h"
#include "misc/eamuse.h"


overlay::Window::Window(SpiceOverlay *overlay) : overlay(overlay) {
}

overlay::Window::~Window() {

    // kill children
    for (auto &child : this->children) {
        delete child;
    }
}

void overlay::Window::update() {

    // check if toggle is enabled
    if (this->toggle_button != ~0u) {

        // get state
        auto overlay_buttons = games::get_buttons_overlay(eamuse_get_game());
        bool toggle_button_new = overlay_buttons
                && this->overlay->hotkeys_triggered()
                && GameAPI::Buttons::getState(RI_MGR, overlay_buttons->at(this->toggle_button));
        if (toggle_button_new && !this->toggle_button_state) {

            // if the overlay is hidden just reactivate it
            if (!this->overlay->get_active()) {
                this->active = true;
                this->overlay->set_active(true);
            } else {
                this->toggle_active();
            }
        }
        this->toggle_button_state = toggle_button_new;
    }

    // update children
    auto it = this->children.begin();
    while (it != this->children.end()) {
        (*it)->update();
        if ((*it)->active) {
            it++;
        } else {
            delete (*it);
            this->children.erase(it);
        }
    }
}

void overlay::Window::build() {

    // check if active
    if (!this->active) {
        return;
    }

    if (this->draws_window) {

        // automatic max window size
        if (!cfg::CONFIGURATOR_STANDALONE && (size_max.x < 0 || size_max.y < 0)) {
            auto &display_size = ImGui::GetIO().DisplaySize;
            ImVec2 size_max_auto(display_size.x - 100, display_size.y - 100);
            ImGui::SetNextWindowSizeConstraints(size_min, size_max_auto, resize_callback);
        } else {
            ImGui::SetNextWindowSizeConstraints(size_min, size_max, resize_callback);
        }

        // background alpha
        if (this->bg_alpha != 1.f) {
            ImGui::SetNextWindowBgAlpha(this->bg_alpha);
        }

        if (this->remove_window_padding) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        }

        // create window
        if (ImGui::Begin(
                (this->title + "###" + to_string(this)).c_str(),
                &this->active,
                this->flags)) {

            // window attributes
            this->calculate_initial_window();
            ImGui::SetWindowPos(this->init_pos, ImGuiCond_Once);
            ImGui::SetWindowSize(this->init_size, ImGuiCond_Once);

            // add content
            this->build_content();

            // build children
            for (auto &child : this->children) {
                child->build();
            }

            // end window
            ImGui::End();
        }

        if (this->remove_window_padding) {
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
        }

    } else {

        // add raw content
        this->build_content();
    }
}

void overlay::Window::toggle_active() {

    // flip bool
    this->active = !this->active;

    // update children
    for (auto &child : this->children) {
        child->toggle_active();
    }
}

void overlay::Window::set_active(bool active) {

    // toggle if different
    if (this->get_active() != active) {

        // flip bool
        this->active = !this->active;

        // update children
        for (auto &child : this->children) {
            child->set_active(this->active);
        }
    }
}

bool overlay::Window::get_active() {

    // check for active children
    for (auto &child : this->children) {
        if (child->get_active()) {
            return true;
        }
    }

    // now it depends on us
    return this->active;
}
