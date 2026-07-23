#include "nostalgia_touch_piano.h"

#include <cmath>

#include "external/imgui/imgui_internal.h"
#include "games/nost/touch_mode.h"

namespace overlay::windows {

    static constexpr float BUTTON_WIDTH = 144.f;
    static constexpr float BUTTON_HEIGHT = 40.f;
    static constexpr float WINDOW_PADDING = 4.f;
    static constexpr float EDGE_MARGIN = 4.f;
    static constexpr float PIANO_HEIGHT_RATIO = 0.08f;
    static constexpr float PIANO_LEFT_GAP = 11.f;
    static constexpr float PIANO_RIGHT_GAP = 10.f;
    static constexpr uint32_t PIANO_KEY_COUNT = 28;

    static constexpr ImU32 PIANO_KEY_COLOR = IM_COL32(255, 255, 255, 50);
    static constexpr ImU32 PIANO_KEY_ACTIVE_COLOR = IM_COL32(255, 48, 48, 160);
    static constexpr ImU32 PIANO_KEY_BORDER_COLOR = IM_COL32(0, 0, 0, 100);

    struct ButtonPalette {
        ImVec4 normal;
        ImVec4 hovered;
        ImVec4 active;
    };

    static const ButtonPalette NAV_MODE_PALETTE {
        ImVec4(0.10f, 0.45f, 0.28f, 0.72f),
        ImVec4(0.14f, 0.58f, 0.36f, 0.82f),
        ImVec4(0.08f, 0.34f, 0.21f, 0.90f),
    };
    static const ButtonPalette PIANO_MODE_PALETTE {
        ImVec4(0.15f, 0.32f, 0.62f, 0.72f),
        ImVec4(0.20f, 0.42f, 0.78f, 0.82f),
        ImVec4(0.10f, 0.24f, 0.50f, 0.90f),
    };

    static void draw_piano_keys(
        const ImVec2 &display_size,
        LONG client_width,
        uint32_t key_state) {

        if (display_size.x <= 0.f || display_size.y <= 0.f || client_width <= 0) {
            return;
        }

        // this is only a visual guide; native touch routing owns the actual input
        const float left_gap = PIANO_LEFT_GAP * display_size.x / client_width;
        const float right_gap = PIANO_RIGHT_GAP * display_size.x / client_width;
        const float piano_width = display_size.x - left_gap - right_gap;
        if (piano_width <= 0.f) {
            return;
        }

        const float key_width = piano_width / PIANO_KEY_COUNT;
        const float key_top = display_size.y * (1.f - PIANO_HEIGHT_RATIO);
        auto *draw_list = ImGui::GetBackgroundDrawList();

        for (uint32_t key = 0; key < PIANO_KEY_COUNT; key++) {
            const ImVec2 key_min(left_gap + key * key_width, key_top);
            const ImVec2 key_max(left_gap + (key + 1) * key_width, display_size.y);
            const bool active = (key_state & (UINT32_C(1) << key)) != 0;
            draw_list->AddRectFilled(
                key_min,
                key_max,
                active ? PIANO_KEY_ACTIVE_COLOR : PIANO_KEY_COLOR);
            draw_list->AddRect(key_min, key_max, PIANO_KEY_BORDER_COLOR);
        }
    }

    NostalgiaTouchPiano::NostalgiaTouchPiano(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Nostalgia Touch Piano";
        this->flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoBackground
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

        const bool nav_mode =
            games::nost::touch_mode::current_mode() == games::nost::touch_mode::Mode::Nav;
        const char *label = nav_mode ? "Nav Mode" : "Piano Mode";

        // make the active routing mode recognizable without reading the label
        const auto &palette = nav_mode ? NAV_MODE_PALETTE : PIANO_MODE_PALETTE;
        ImGui::PushStyleColor(ImGuiCol_Button, palette.normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.active);
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
            if (!nav_mode) {
                draw_piano_keys(
                    io.DisplaySize,
                    client_width,
                    games::nost::touch_mode::piano_key_state());
            }

            RECT button_bounds {
                static_cast<LONG>(std::lround(item_min.x * client_width / io.DisplaySize.x)),
                static_cast<LONG>(std::lround(item_min.y * client_height / io.DisplaySize.y)),
                static_cast<LONG>(std::lround(item_max.x * client_width / io.DisplaySize.x)),
                static_cast<LONG>(std::lround(item_max.y * client_height / io.DisplaySize.y)),
            };
            games::nost::touch_mode::publish_button_bounds(
                this->overlay->get_window(), button_bounds);
        }
    }
}
