#include "nostalgia_touch_piano.h"

#include <cmath>

#include "external/imgui/imgui_internal.h"
#include "games/nost/touch_mode.h"

namespace overlay::windows {

    static constexpr float BUTTON_WIDTH = 144.f;
    static constexpr float BUTTON_HEIGHT = 40.f;
    static constexpr float WINDOW_PADDING = 4.f;
    static constexpr float EDGE_MARGIN = 4.f;

    NostalgiaTouchPiano::NostalgiaTouchPiano(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Nostalgia Touch Piano";
        this->flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoBringToFrontOnFocus;
        this->window_padding = overlay::apply_scaling_to_vector(WINDOW_PADDING, WINDOW_PADDING);
        this->set_active(true);
    }

    void NostalgiaTouchPiano::calculate_initial_window() {
        this->init_size = overlay::apply_scaling_to_vector(
            BUTTON_WIDTH + WINDOW_PADDING * 2,
            BUTTON_HEIGHT + WINDOW_PADDING * 2);
        this->init_pos = overlay::apply_scaling_to_vector(EDGE_MARGIN, EDGE_MARGIN);
    }

    void NostalgiaTouchPiano::build_content() {
        // keep the control anchored while the game window changes size or mode
        ImGui::SetWindowPos(
            overlay::apply_scaling_to_vector(EDGE_MARGIN, EDGE_MARGIN),
            ImGuiCond_Always);

        // stay above regular overlay windows, but never cover a blocking modal
        ImGuiWindow *mode_window = ImGui::GetCurrentWindow();
        if (ImGuiWindow *modal = ImGui::GetTopMostPopupModal()) {
            ImGui::BringWindowToDisplayBehind(mode_window, modal);
        } else {
            ImGui::BringWindowToDisplayFront(mode_window);
        }

        const auto mode = games::nost::touch_mode::current_mode();
        const char *label = mode == games::nost::touch_mode::Mode::Touch
            ? "Nav Mode"
            : "Piano Mode";

        // make the active routing mode recognizable without reading the label
        const bool touch_mode = mode == games::nost::touch_mode::Mode::Touch;
        const ImVec4 button_color = touch_mode
            ? ImVec4(0.10f, 0.45f, 0.28f, 1.f)
            : ImVec4(0.15f, 0.32f, 0.62f, 1.f);
        const ImVec4 hovered_color = touch_mode
            ? ImVec4(0.14f, 0.58f, 0.36f, 1.f)
            : ImVec4(0.20f, 0.42f, 0.78f, 1.f);
        const ImVec4 active_color = touch_mode
            ? ImVec4(0.08f, 0.34f, 0.21f, 1.f)
            : ImVec4(0.10f, 0.24f, 0.50f, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Button, button_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active_color);
        ImGui::Button(label, overlay::apply_scaling_to_vector(BUTTON_WIDTH, BUTTON_HEIGHT));
        ImGui::PopStyleColor(3);

        const auto &io = ImGui::GetIO();
        RECT client_rect {};
        if (io.DisplaySize.x > 0.f && io.DisplaySize.y > 0.f &&
            GetClientRect(this->overlay->get_window(), &client_rect)) {

            // convert the rendered imgui rectangle into the client coordinates
            // used by hardware touch publication and piano-key mapping
            const auto item_min = ImGui::GetItemRectMin();
            const auto item_max = ImGui::GetItemRectMax();
            const auto client_width = client_rect.right - client_rect.left;
            const auto client_height = client_rect.bottom - client_rect.top;
            RECT button_bounds {
                static_cast<LONG>(std::lround(item_min.x * client_width / io.DisplaySize.x)),
                static_cast<LONG>(std::lround(item_min.y * client_height / io.DisplaySize.y)),
                static_cast<LONG>(std::lround(item_max.x * client_width / io.DisplaySize.x)),
                static_cast<LONG>(std::lround(item_max.y * client_height / io.DisplaySize.y)),
            };
            games::nost::touch_mode::publish_button_bounds(
                this->overlay->get_window(), button_bounds);
        }

        const bool mode_button_activated =
            games::nost::touch_mode::consume_mode_button_activation();
        if (mode_button_activated) {
            games::nost::touch_mode::toggle();
        }
    }
}