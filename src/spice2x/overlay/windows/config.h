#pragma once

#include <chrono>
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

    struct MatchEntry {
        std::string game_name;
        std::string device_name;
        int light_index;
        int control_index;
        bool soft;
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

        void inc_buttons_many_index(int index_max);

        // analogs tab
        std::vector<rawinput::Device *> analogs_devices;
        int analogs_devices_selected = -1;
        int analogs_devices_control_selected = -1;

        // lights tab
        std::vector<rawinput::Device *> lights_devices;
        int lights_devices_selected = -1;
        int lights_devices_control_selected = -1;

        // lights test all
        bool lights_testing = false;
        int lights_test_current = -1;
        std::chrono::steady_clock::time_point lights_test_time;

        // lights auto match
        std::vector<rawinput::Device *> auto_match_devices;
        int auto_match_device_selected = -1;
        bool auto_match_testing = false;
        int auto_match_test_current = -1;
        std::chrono::steady_clock::time_point auto_match_test_time;
        std::string auto_match_test_device;
        unsigned int auto_match_test_control = 0;
        std::chrono::steady_clock::time_point auto_match_copy_time;
        bool auto_match_copied = false;
        bool auto_match_soft_enabled = true;
        bool auto_match_p2 = false;

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

        std::filesystem::path file_picker_path;
        std::thread *file_picker_thread = nullptr;
        bool file_picker_done = false;

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
        void clear_button(Button *button, const int alt_index, std::optional<unsigned short> vKey_default = std::nullopt);

        void build_analogs(const std::string &name, std::vector<Analog> *analogs);
        void edit_analog_popup(Analog &analog);

        void update() override;
        void stop_lights_test();
        void build_lights(const std::string &name, std::vector<Light> *lights);
        void build_light(Light &primary_light, Light *light, const int light_index, const int alt_index);
        void clear_light(Light *light, const int alt_index);
        void edit_light_popup(Light &primary_light, Light *light, const int alt_index);
        void auto_match_lights_popup(std::vector<Light> *lights);

        std::string match_lights(
            rawinput::Device *device,
            std::vector<Light> *lights,
            std::vector<std::string> &raw_names,
            std::vector<MatchEntry> &matched,
            std::vector<MatchEntry> &unmatched);

        void build_cards();
        std::string build_option_value_picker_title(const OptionDefinition& option);
        void build_option_value_picker(Option& option);
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
