#pragma once

#include <optional>

#include "cfg/game.h"
#include "overlay/window.h"
#include "rawinput/device.h"
#include "external/imgui/imgui_filebrowser.h"
#include "patch_manager.h"

namespace overlay::windows {

    enum class ConfigTab {
        CONFIG_TAB_INVALID,
        CONFIG_TAB_BUTTONS,
        CONFIG_TAB_ANALOGS,
        CONFIG_TAB_OVERLAY,
        CONFIG_TAB_LIGHTS,
        CONFIG_TAB_CARDS,
        CONFIG_TAB_PATCHES,
        CONFIG_TAB_API,
        CONFIG_TAB_OPTIONS,
        CONFIG_TAB_ADVANCED,
        CONFIG_TAB_DEV,
        CONFIG_TAB_SEARCH,
    };

    class Config : public Window {
    private:

        // game selection
        int games_selected = -1;
        std::string games_selected_name = "";
        std::vector<Game> games_list;
        std::vector<const char *> games_names;

        // tabs ui
        ConfigTab tab_selected = ConfigTab::CONFIG_TAB_INVALID;

        // buttons tab
        bool buttons_keyboard_state[0xFF];
        bool buttons_bind_active = false;
        bool buttons_many_active = false;
        std::string buttons_many_active_section = "";
        bool buttons_many_naive = false;
        int buttons_many_delay = 0;
        int buttons_many_index = -1;
        
        bool io_allow_multi_binding = false;
        bool io_has_valid_alternatives = false;

        void inc_buttons_many_index(int index_max);

        // analogs tab
        std::vector<rawinput::Device *> analogs_devices;
        int analogs_devices_selected = -1;
        int analogs_devices_control_selected = -1;

        // lights tab
        std::vector<rawinput::Device *> lights_devices;
        int lights_devices_selected = -1;
        int lights_devices_control_selected = -1;

        // keypads tab
        int keypads_selected[2] {};
        char keypads_card_path[2][1024] {};
        std::thread *keypads_card_select = nullptr;
        bool keypads_card_select_done = false;
        ImGui::FileBrowser keypads_card_select_browser[2];
        char keypads_card_number[2][18] {};

        // patches tab
        std::unique_ptr<PatchManager> patch_manager;

        // options tab
        bool options_show_hidden = false;
        bool options_dirty = false;
        int options_category = 0;
        std::string search_filter = "";
        std::string search_filter_in_lower_case = "";

        void build_buttons(const std::string &name, std::vector<Button> *buttons, int min = 0, int max = -1);
        void build_button(
            const std::string &name,
            Button &primary_button,
            Button *button,
            const int button_it,
            const int button_it_max,
            const int alt_index);

        void bind_button_popup(const std::string &bind_name, Button *button, const int button_it_max, const int alt_index);
        void naive_button_popup(const std::string &naive_string, Button *button, const int button_it_max, const int alt_index);
        void edit_button_popup(
            const std::string &edit_name,
            const std::string &button_display,
            Button *button,
            const float button_velocity,
            const int alt_index);
        void clear_button(Button *button, const int alt_index);
        void bind_multiple_checkbox();

        void build_analogs(const std::string &name, std::vector<Analog> *analogs);
        void edit_analog_popup(Analog &analog);

        void build_lights(const std::string &name, std::vector<Light> *lights);
        void build_light(Light &primary_light, Light *light, const int light_index, const int alt_index);
        void clear_light(Light *light, const int alt_index);
        void edit_light_popup(Light &primary_light, Light *light, const int alt_index);

        void build_cards();
        void build_options(
            std::vector<Option> *options, const std::string &category, const std::string *filter=nullptr);
        void build_about();
        void build_launcher();
        void launch_shell(LPCSTR app, LPCSTR file=nullptr);

        void build_menu(int *game_selected);
        void shutdown_system(bool force, bool reboot_instead);

        void set_alternating_row_colors(const int row_index);

    public:
        Config(SpiceOverlay *overlay);
        ~Config() override;

        void read_card(int player = -1);
        void write_card(int player);
        void build_content() override;

    };
}
