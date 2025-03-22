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
        this->toggle_scene[0] = games::OverlayButtons::ScreenResizeScene1;
        this->toggle_scene[1] = games::OverlayButtons::ScreenResizeScene2;
        this->toggle_scene[2] = games::OverlayButtons::ScreenResizeScene3;
        this->toggle_scene[3] = games::OverlayButtons::ScreenResizeScene4;
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
        cfg::SCREENRESIZE->screen_resize_current_scene = 0;
        cfg::SCREENRESIZE->enable_linear_filter = true;
        for (size_t i = 0; i < std::size(cfg::SCREENRESIZE->scene_settings); i++) {
            auto& scene = cfg::SCREENRESIZE->scene_settings[i];
            scene.keep_aspect_ratio = true;
            scene.centered = true;
            scene.offset_x = 0;
            scene.offset_y = 0;
            scene.scale_x = 1.f;
            scene.scale_y = 1.f;
        }

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
        ImGui::Checkbox("Enable", &cfg::SCREENRESIZE->enable_screen_resize);
        ImGui::SameLine();
        ImGui::HelpMarker("Hint: bind a key to Screen Resize for a quick toggle.");

        ImGui::BeginDisabled(!cfg::SCREENRESIZE->enable_screen_resize);
        ImGui::Checkbox("Linear Filter", &cfg::SCREENRESIZE->enable_linear_filter);

        if (ImGui::RadioButton("Scene 1", cfg::SCREENRESIZE->screen_resize_current_scene == 0)) {
            cfg::SCREENRESIZE->screen_resize_current_scene = 0;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scene 2", cfg::SCREENRESIZE->screen_resize_current_scene == 1)) {
            cfg::SCREENRESIZE->screen_resize_current_scene = 1;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scene 3", cfg::SCREENRESIZE->screen_resize_current_scene == 2)) {
            cfg::SCREENRESIZE->screen_resize_current_scene = 2;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scene 4", cfg::SCREENRESIZE->screen_resize_current_scene == 3)) {
            cfg::SCREENRESIZE->screen_resize_current_scene = 3;
        }
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Hint: bind a key to Screen Resize 1/2/3/4 for quick scene switching. "
            "Scene 1 is the default scene activated when starting the game.");

        auto& scene = cfg::SCREENRESIZE->scene_settings[cfg::SCREENRESIZE->screen_resize_current_scene];

        // general settings
        ImGui::Checkbox("Centered", &scene.centered);
        if (!scene.centered) {
            ImGui::InputInt("X Offset", &scene.offset_x);
            ImGui::InputInt("Y Offset", &scene.offset_y);
        }

        // aspect ratio
        ImGui::Checkbox("Keep Aspect Ratio", &scene.keep_aspect_ratio);
        if (scene.keep_aspect_ratio) {
            if (ImGui::SliderFloat("Scale", &scene.scale_x, 0.65f, 2.0f)) {
                scene.scale_y = scene.scale_x;
            }
        } else {
            ImGui::SliderFloat("Width Scale", &scene.scale_x, 0.65f, 2.0f);
            ImGui::SliderFloat("Height Scale", &scene.scale_y, 0.65f, 2.0f);
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
        auto overlay_buttons = games::get_buttons_overlay(eamuse_get_game());

        // toggle
        if (this->toggle_screen_resize != ~0u) {
            bool toggle_screen_resize_new = overlay_buttons
                && this->overlay->hotkeys_triggered()
                && GameAPI::Buttons::getState(RI_MGR, overlay_buttons->at(this->toggle_screen_resize));
            
            if (toggle_screen_resize_new && !this->toggle_screen_resize_state) {
                cfg::SCREENRESIZE->enable_screen_resize = !cfg::SCREENRESIZE->enable_screen_resize;
            }
            this->toggle_screen_resize_state = toggle_screen_resize_new;
        }

        // scene switch
        auto toggle_scene_state_new = ~0u;
        for (size_t i = 0; i < std::size(this->toggle_scene); i++) {
            if (this->toggle_scene[i] == ~0u) {
                continue;
            }
            bool scene_switched = overlay_buttons
                && this->overlay->hotkeys_triggered()
                && GameAPI::Buttons::getState(RI_MGR, overlay_buttons->at(this->toggle_scene[i]));

            if (scene_switched) {
                toggle_scene_state_new = (uint32_t)i;
            }

            // only detect rising edges of key presses
            if (scene_switched && (this->toggle_scene_state != i)) {
                if (cfg::SCREENRESIZE->screen_resize_current_scene == (int8_t)i &&
                    cfg::SCREENRESIZE->enable_screen_resize) {
                    // this scene is already active, turn scaling off
                    cfg::SCREENRESIZE->enable_screen_resize = false;
                } else {
                    // switch to scene
                    cfg::SCREENRESIZE->enable_screen_resize = true;
                    cfg::SCREENRESIZE->screen_resize_current_scene = i;
                }
                break;
            }
        }
        // remember if a key was pressed (or nothing pressed) this frame
        this->toggle_scene_state = toggle_scene_state_new;
    }
}
