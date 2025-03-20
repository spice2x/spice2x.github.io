#include <games/io.h>

#include "screen_resize.h"
#include "avs/game.h"
#include "cfg/screen_resize.h"
#include "hooks/graphics/graphics.h"
#include "overlay/imgui/extensions.h"
#include "misc/eamuse.h"
#include "util/logging.h"
#include "util/utils.h"

namespace overlay::windows {

    ScreenResize::ScreenResize(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Screen Resize";
        this->flags = ImGuiWindowFlags_AlwaysAutoResize;
        this->init_pos = ImVec2(10, 10);
        this->toggle_button = games::OverlayButtons::ToggleScreenResize;
        this->toggle_screen_resize = games::OverlayButtons::ScreenResize;
    }

    ScreenResize::~ScreenResize() {
    }

    HWND ScreenResize::get_first_window() {
        if (GRAPHICS_WINDOWS.size() == 0) {
            return NULL;
        }
        return GRAPHICS_WINDOWS[0];
    }

    void ScreenResize::reset_window() {
        this->reset_vars_to_default();
        if (GRAPHICS_WINDOWED) {
            const auto window = get_first_window();
            if (window) {
                graphics_move_resize_window(window);
                graphics_update_window_style(window);
            }
        }
    }

    void ScreenResize::reset_vars_to_default() {
        cfg::SCREENRESIZE->enable_screen_resize = false;
        cfg::SCREENRESIZE->enable_linear_filter = true;
        cfg::SCREENRESIZE->keep_aspect_ratio = true;
        cfg::SCREENRESIZE->centered = true;
        cfg::SCREENRESIZE->offset_x = 0;
        cfg::SCREENRESIZE->offset_y = 0;
        cfg::SCREENRESIZE->scale_x = 1.f;
        cfg::SCREENRESIZE->scale_y = 1.f;

        cfg::SCREENRESIZE->enable_window_resize = false;
        cfg::SCREENRESIZE->window_always_on_top = false;
        cfg::SCREENRESIZE->window_decoration = cfg::WindowDecorationMode::Default;
        cfg::SCREENRESIZE->window_offset_x = 0;
        cfg::SCREENRESIZE->window_offset_y = 0;
        cfg::SCREENRESIZE->client_keep_aspect_ratio = true;
        cfg::SCREENRESIZE->client_width = cfg::SCREENRESIZE->init_client_width;
        cfg::SCREENRESIZE->client_height = cfg::SCREENRESIZE->init_client_height;
    }

    void ScreenResize::build_content() {
        ImGui::Text("For: %s", eamuse_get_game().c_str());
        {
            if (ImGui::TreeNodeEx("Image Resize", ImGuiTreeNodeFlags_DefaultOpen)) {
                this->build_fullscreen_config();
                ImGui::TreePop();
            }
        }

        ImGui::BeginDisabled(!GRAPHICS_WINDOWED);
        {
            int flags = 0;
            if (GRAPHICS_WINDOWED) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (ImGui::TreeNodeEx("Window Size", flags)) {
                build_windowed_config();
                ImGui::TreePop();
            }
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        build_footer();
    }

    void ScreenResize::build_fullscreen_config() {
        // enable checkbox
        ImGui::TextWrapped("Hint: bind a key to Screen Resize for quickly toggling this on/off.");
        ImGui::Checkbox("Enable", &cfg::SCREENRESIZE->enable_screen_resize);

        ImGui::BeginDisabled(!cfg::SCREENRESIZE->enable_screen_resize);

        // general settings
        ImGui::Checkbox("Linear Filter", &cfg::SCREENRESIZE->enable_linear_filter);
        ImGui::Checkbox("Centered", &cfg::SCREENRESIZE->centered);
        if (!cfg::SCREENRESIZE->centered) {
            ImGui::InputInt("X Offset", &cfg::SCREENRESIZE->offset_x);
            ImGui::InputInt("Y Offset", &cfg::SCREENRESIZE->offset_y);
        }

        // aspect ratio
        ImGui::Checkbox("Keep Aspect Ratio", &cfg::SCREENRESIZE->keep_aspect_ratio);
        if (cfg::SCREENRESIZE->keep_aspect_ratio) {
            if (ImGui::SliderFloat("Scale", &cfg::SCREENRESIZE->scale_x, 0.65f, 2.0f)) {
                cfg::SCREENRESIZE->scale_y = cfg::SCREENRESIZE->scale_x;
            }
        } else {
            ImGui::SliderFloat("Width Scale", &cfg::SCREENRESIZE->scale_x, 0.65f, 2.0f);
            ImGui::SliderFloat("Height Scale", &cfg::SCREENRESIZE->scale_y, 0.65f, 2.0f);
        }

        ImGui::EndDisabled();
    }

    void ScreenResize::build_windowed_config() {
        // for now, only supports the first window
        const auto window = get_first_window();
        if (!window) {
            return;
        }

        ImGui::TextUnformatted("Warning: may cause some games to crash.");
        ImGui::BeginDisabled(graphics_window_change_crashes_game());
        if (ImGui::Combo(
                "Window Style",
                &cfg::SCREENRESIZE->window_decoration,
                "Default\0Borderless\0Resizable Window\0\0")) {
            graphics_update_window_style(window);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Change window decoration. Resizable Window may not cause your mouse cursor to change, "
            "but you can still drag to resize. Disabled for some games due to incompatibility.");

        if (ImGui::Checkbox("Always on Top", &cfg::SCREENRESIZE->window_always_on_top) ) {
            graphics_update_z_order(window);
        }
        ImGui::BeginDisabled();
        ImGui::Checkbox("Forced Render Scaling", &GRAPHICS_WINDOW_BACKBUFFER_SCALE);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::HelpMarker(
            "For windowed mode: forcibly set DX9 back buffer dimensions to match window size. "
            "Reduces pixelated scaling artifacts. Works great on some games, but completely broken on others.\n\n"
            "This can't be changed in-game; instead, set -windowscale option in spicecfg and restart.");

        ImGui::Checkbox("Keep Aspect Ratio", &cfg::SCREENRESIZE->client_keep_aspect_ratio);
        ImGui::Checkbox("Manual window move/resize", &cfg::SCREENRESIZE->enable_window_resize);
        ImGui::BeginDisabled(!cfg::SCREENRESIZE->enable_window_resize);

        bool changed = false;
        const uint32_t step = 1;
        const uint32_t step_fast = 10;
        ImGui::BeginDisabled(cfg::SCREENRESIZE->client_keep_aspect_ratio);
        ImGui::InputScalar(
            "Width",
            ImGuiDataType_U32,
            &cfg::SCREENRESIZE->client_width,
            &step, &step_fast, nullptr);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();

        ImGui::InputScalar(
            "Height",
            ImGuiDataType_U32,
            &cfg::SCREENRESIZE->client_height,
            &step, &step_fast, nullptr);
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::InputScalar(
            "X Offset",
            ImGuiDataType_S32,
            &cfg::SCREENRESIZE->window_offset_x,
            &step, &step_fast, nullptr);
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::InputScalar(
            "Y Offset",
            ImGuiDataType_S32,
            &cfg::SCREENRESIZE->window_offset_y,
            &step, &step_fast, nullptr);
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        if (changed) {
            if (cfg::SCREENRESIZE->client_keep_aspect_ratio) {
                cfg::SCREENRESIZE->client_width =
                    cfg::SCREENRESIZE->client_height * cfg::SCREENRESIZE->init_client_aspect_ratio;
            }
            graphics_move_resize_window(window);
        }

        ImGui::EndDisabled();
    }

    void ScreenResize::build_footer() {
        // reset button
        if (ImGui::Button("Reset")) {
            this->reset_window();
        }

        // load button
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            cfg::SCREENRESIZE->config_load();
        }

        // save button
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            cfg::SCREENRESIZE->config_save();
        }
    }

    void ScreenResize::update() {
        Window::update();
        if (this->toggle_screen_resize != ~0u) {
            auto overlay_buttons = games::get_buttons_overlay(eamuse_get_game());
            bool toggle_screen_resize_new = overlay_buttons
                && this->overlay->hotkeys_triggered()
                && GameAPI::Buttons::getState(RI_MGR, overlay_buttons->at(this->toggle_screen_resize));
            
            if (toggle_screen_resize_new && !this->toggle_screen_resize_state) {
                cfg::SCREENRESIZE->enable_screen_resize = !cfg::SCREENRESIZE->enable_screen_resize;
            }
            this->toggle_screen_resize_state = toggle_screen_resize_new;
        }
    }
}
