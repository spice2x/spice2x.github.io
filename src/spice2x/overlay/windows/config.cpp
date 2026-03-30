#include "config.h"

#include <algorithm>
#include <thread>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>

#include "build/defs.h"
#include "build/resource.h"
#include "cfg/config.h"
#include "cfg/configurator.h"
#include "external/asio/asiolist.h"
#include "external/imgui/imgui_internal.h"
#include "external/imgui/misc/cpp/imgui_stdlib.h"
#include "games/io.h"
#include "games/sdvx/sdvx.h"
#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
#include "light_match_map.h"
#include "launcher/launcher.h"
#include "launcher/options.h"
#include "misc/eamuse.h"
#include "overlay/imgui/extensions.h"
#include "rawinput/piuio.h"
#include "rawinput/rawinput.h"
#include "rawinput/touch.h"
#include "misc/clipboard.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/resutils.h"
#include "util/scope_guard.h"
#include "util/time.h"
#include "util/utils.h"

#ifdef min
#undef min
#endif

static int CALLBACK BrowseCallbackProc(
    HWND hwnd,
    UINT uMsg,
    LPARAM lParam,
    LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
    }
    return 0;
}

namespace overlay::windows {

    // same width as dummy marker
    const float INDENT = 22.f;
    const auto PROJECT_URL = "https://spice2x.github.io";

    constexpr ImVec4 TEXT_COLOR_GREEN(0.f, 1.f, 0.f, 1.f);
    constexpr uint32_t OPTION_INPUT_TEXT_WIDTH = 512;
    
    std::unique_ptr<AsioDriverList> asio_driver_list;

    Config::Config(overlay::SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Configuration";
        this->toggle_button = games::OverlayButtons::ToggleConfig;
        this->init_size = overlay::apply_scaling_to_vector(ImVec2(800, 600));
        this->size_min = overlay::apply_scaling_to_vector(ImVec2(100, 200));
        this->init_pos = ImVec2(0, 0);
        if (cfg::CONFIGURATOR_STANDALONE && cfg::CONFIGURATOR_TYPE == cfg::ConfigType::Config) {
            this->active = true;
            this->flags |= ImGuiWindowFlags_NoResize;
            this->flags |= ImGuiWindowFlags_NoMove;
            this->flags |= ImGuiWindowFlags_NoTitleBar;
            this->flags |= ImGuiWindowFlags_NoCollapse;
            this->flags |= ImGuiWindowFlags_NoDecoration;
        }
        this->flags |= ImGuiWindowFlags_MenuBar;

        // build game list
        auto &game_names = games::get_games();
        for (auto &game_name : game_names) {
            this->games_names.push_back(game_name.c_str());
            auto &game = this->games_list.emplace_back(game_name);
            auto buttons = games::get_buttons(game_name);
            auto analogs = games::get_analogs(game_name);
            auto lights = games::get_lights(game_name);
            if (buttons) {
                for (auto &item : *buttons) {
                    game.addItems(item);
                }
            }
            if (analogs) {
                for (auto &item : *analogs) {
                    game.addItems(item);
                }
            }
            if (lights) {
                for (auto &item : *lights) {
                    game.addItems(item);
                }
            }

            // default to currently running game
            if (!cfg::CONFIGURATOR_STANDALONE && game_name == eamuse_get_game()) {
                this->games_selected = games_list.size() - 1;
                this->games_selected_name = game_name;
            }

            // standalone configurator should look for file hints
            if (cfg::CONFIGURATOR_STANDALONE) {
                const auto file_hints = games::get_game_file_hints(game_name);
                if (file_hints) {
                    for (auto &file_hint : *file_hints) {
                        if (fileutils::file_exists(file_hint) ||
                            fileutils::dir_exists(file_hint) ||
                            fileutils::file_exists(std::filesystem::path("modules") / file_hint) ||
                            fileutils::file_exists(std::filesystem::path("contents") / file_hint) ||
                            fileutils::file_exists(MODULE_PATH / file_hint))
                        {
                            this->games_selected = games_list.size() - 1;
                            this->games_selected_name = game_name;
                            eamuse_set_game(game_name);
                            break;
                        }
                    }
                }
            }
        }

        // configurator fallback to detected game name
        if (cfg::CONFIGURATOR_STANDALONE && this->games_selected == -1) {
            for (size_t i = 0; i < games_names.size(); i++) {
                if (games_names[i] == eamuse_get_game()) {
                    this->games_selected = i;
                }
            }
        }

        // add games to the config and window
        auto &config = ::Config::getInstance();
        for (auto &game : games_list) {
            config.addGame(game);

            if (!config.getStatus()) {
                log_warning("config", "failure adding game: {}", game.getGameName());
            }
        }

        // read card numbers
        read_card();
    }

    Config::~Config() {
    }

    void Config::inc_buttons_many_index(int index_max) {
        if (this->buttons_many_index == index_max) {
            this->buttons_many_index = -1;
        } else {
            this->buttons_many_index += 1;
        }
    }

    void Config::read_card(int player) {

        // check if a game is selected
        if (this->games_selected_name.empty()) {
            return;
        }

        // iterate bindings
        auto bindings = ::Config::getInstance().getKeypadBindings(this->games_selected_name);
        for (int p = 0; p < 2; ++p) {
            if (player < 0 || player == p) {

                // get path
                std::filesystem::path path;
                if (!bindings.card_paths[p].empty()) {
                    path = bindings.card_paths[p];
                } else {
                    path = p > 0 ? "card1.txt" : "card0.txt";
                }

                // open file
                std::ifstream f(path);
                if (!f || !f.is_open()) {
                    this->keypads_card_number[p][0] = 0;
                    continue;
                }

                // get file size
                f.seekg(0, f.end);
                auto length = (size_t) f.tellg();
                f.seekg(0, f.beg);

                // read file contents
                f.read(this->keypads_card_number[p], 16);
                this->keypads_card_number[p][length < 16 ? length : 16] = 0;
                f.close();
            }
        }
    }

    void Config::build_content() {

        // if standalone then fullscreen window
        if (cfg::CONFIGURATOR_STANDALONE) {
            ImGui::SetWindowPos(ImVec2(0, 0));
            ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);
        }

        // toolbar/menu
        int previous_games_selected = this->games_selected;
        this->build_menu(&this->games_selected);

        // remember selected game name
        if (this->games_selected >= 0 && this->games_selected < (int) games_list.size()) {
            this->games_selected_name = games_list.at(games_selected).getGameName();

            // standalone configurator applies selected game
            if (cfg::CONFIGURATOR_STANDALONE) {
                eamuse_set_game(games_selected_name);
            }

        } else {

            // invalid selection
            this->games_selected_name = "";
        }

        // display launcher if no game is selected
        if (this->games_selected_name.empty()) {
            this->build_launcher();
            return;
        }

        // selected game changed
        if (previous_games_selected != this->games_selected) {
            read_card();
            templates_cache_dirty = true;
            log_misc(
                "config",
                "game selection changed from {} to {}",
                previous_games_selected, this->games_selected);
        }

        // tab selection
        auto tab_selected_new = ConfigTab::CONFIG_TAB_INVALID;
        if (ImGui::BeginTabBar("Config Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
            const int page_offset = overlay::apply_scaling(cfg::CONFIGURATOR_STANDALONE ? 88 : 110);
            const int page_offset2 = overlay::apply_scaling(cfg::CONFIGURATOR_STANDALONE ? 65 : 87);

            if (ImGui::BeginTabItem("Buttons")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_BUTTONS;
                ImGui::BeginChild("Buttons", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);

                // help text for binding buttons, if the game has one
                const auto help_text = games::get_buttons_help(this->games_selected_name);
                if (!help_text.empty()) {
                    ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Button Bindings");
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", help_text.c_str());
                    ImGui::TextUnformatted("");
                }

                // game buttons
                this->build_buttons("Game", games::get_buttons(this->games_selected_name));

                // keypad buttons
                ImGui::TextUnformatted("");
                if (this->games_selected_name == "Beatmania IIDX") {
                    ImGui::Indent(INDENT);
                    ImGui::TextColored(
                        ImVec4(1, 0.5f, 0.5f, 1.f),
                        "WARNING: Lightning Model (TDJ) I/O will ignore the keypad!");
                    ImGui::TextWrapped(
                        "Use Toggle Sub Screen button to show the overlay and use your mouse, "
                        "connect using SpiceCompanion app, or connect a touch screen to enter "
                        "the PIN.");
                    ImGui::Unindent(INDENT);
                    ImGui::TextUnformatted("");
                }
                auto keypad_buttons = games::get_buttons_keypads(this->games_selected_name);
                auto keypad_count = eamuse_get_game_keypads_name();
                if (keypad_count == 1) {
                    this->build_buttons("Keypad", keypad_buttons,
                                        0, games::KeypadButtons::Size - 1);
                } else if (keypad_count >= 2) {
                    this->build_buttons("Keypad", keypad_buttons);
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Analogs")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_ANALOGS;

                ImGui::BeginChild("Analogs", ImVec2(
                    0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);

                // help text for binding analog, if the game has one
                const auto help_text = games::get_analogs_help(this->games_selected_name);
                if (!help_text.empty()) {
                    ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Analog Bindings");
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", help_text.c_str());
                    ImGui::TextUnformatted("");
                }
                this->build_analogs("Game", games::get_analogs(this->games_selected_name));
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Overlay")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_OVERLAY;

                const auto offset = cfg::CONFIGURATOR_STANDALONE ? page_offset : page_offset2;

                ImGui::BeginChild("Overlay", ImVec2(
                    0, ImGui::GetWindowContentRegionMax().y - offset), false);

                // overlay buttons
                this->build_buttons("Overlay", games::get_buttons_overlay(this->games_selected_name));
                ImGui::EndChild();

                // standalone configurator extras
                if (cfg::CONFIGURATOR_STANDALONE) {
                    ImGui::Checkbox("Enable Overlay in Config", &OVERLAY->hotkeys_enable);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Lights")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_LIGHTS;

                ImGui::BeginChild("Lights", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);
                this->build_lights("Game", games::get_lights(this->games_selected_name));
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Presets")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_PRESETS;
                ImGui::BeginChild("Presets", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);
                this->build_presets();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Cards")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_CARDS;
                ImGui::BeginChild("Cards", ImVec2(
                    0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);
                this->build_cards();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Patches")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_PATCHES;

                // initialization
                static std::once_flag initialized;
                static bool failure = false;
                std::call_once(initialized, [this] {
                    if (cfg::CONFIGURATOR_STANDALONE) {

                        // verify game is set, otherwise set failure flag
                        if (strlen(avs::game::MODEL) != 3
                        || (strlen(avs::game::DEST) != 1)
                        || (strlen(avs::game::SPEC) != 1)
                        || (strlen(avs::game::REV) != 1)
                        || (strlen(avs::game::EXT) != 10)
                        || (strcmp(avs::game::MODEL, "000") == 0)
                        || (strcmp(avs::game::EXT, "0000000000") == 0)) {
                            failure = true;
                        }
                    }
                });

                // display tab contents
                ImGui::BeginChild("Patches", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);
                if (failure) {
                    ImGui::TextColored(ImVec4(0.7f, 0.f, 0.f, 1.f),
                            "Unable to detect the game version.\n"
                            "Try to open Patch Manager using the game overlay.");
                } else {

                    // allocate patch manager
                    if (!patch_manager) {
                        patch_manager.reset(new PatchManager(overlay));
                    }

                    // display patch manager
                    this->patch_manager->build_content();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("API")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_API;

                // API options list
                ImGui::BeginChild("ApiTab", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);
                auto options = games::get_options(this->games_selected_name);
                for (auto category : launcher::get_categories(launcher::Options::OptionsCategory::API)) {
                    this->build_options(options, category);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Options")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_OPTIONS;

                // options list
                ImGui::BeginChild("Options", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset), false);
                auto options = games::get_options(this->games_selected_name);
                for (auto category : launcher::get_categories(launcher::Options::OptionsCategory::Basic)) {
                    this->build_options(options, category);
                }

                ImGui::EndChild();

                // hidden options checkbox
                ImGui::Checkbox("Show Hidden Options", &this->options_show_hidden);
                if (!cfg::CONFIGURATOR_STANDALONE && this->options_dirty) {
                    ImGui::SameLine();
                    if (ImGui::Button("Restart Game")) {
                        launcher::restart();
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker("You need to restart the game to apply the changed settings.");
                }

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_ADVANCED;

                // advanced options list
                ImGui::BeginChild("AdvancedOptions", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset), false);
                auto options = games::get_options(this->games_selected_name);
                for (auto category : launcher::get_categories(launcher::Options::OptionsCategory::Advanced)) {
                    this->build_options(options, category);
                }
                ImGui::EndChild();

                // hidden options checkbox
                ImGui::Checkbox("Show Hidden Options", &this->options_show_hidden);
                if (!cfg::CONFIGURATOR_STANDALONE && this->options_dirty) {
                    ImGui::SameLine();
                    if (ImGui::Button("Restart Game")) {
                        launcher::restart();
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker("You need to restart the game to apply the changed settings.");
                }

                // reset configuration button
                ImGui::SameLine();
                if (ImGui::Button("Reset Configuration")) {
                    ImGui::OpenPopup("Reset Config");
                }
                if (ImGui::BeginPopupModal("Reset Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1.f),
                            "Do you really want to reset your configuration for all games?\n"
                            "Warning: This can't be reverted!");
                    if (ImGui::Button("Yes")) {
                        ::Config::getInstance().createConfigFile();
                        launcher::restart();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Nope")) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Development")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_DEV;

                // dev options list
                ImGui::BeginChild("DevOptions", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset), false);
                auto options = games::get_options(this->games_selected_name);
                for (auto category : launcher::get_categories(launcher::Options::OptionsCategory::Dev)) {
                    this->build_options(options, category);
                }

                ImGui::EndChild();

                // hidden options checkbox
                ImGui::Checkbox("Show Hidden Options", &this->options_show_hidden);
                if (!cfg::CONFIGURATOR_STANDALONE && this->options_dirty) {
                    ImGui::SameLine();
                    if (ImGui::Button("Restart Game")) {
                        launcher::restart();
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker("You need to restart the game to apply the changed settings.");
                }

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Search")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_SEARCH;

                ImGui::BeginChild("SearchOptions", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset), false);
                        
                // search from all options
                ImGui::Spacing();
                ImGui::SetNextItemWidth(420.f);
                if (ImGui::InputTextWithHint(
                        "", "Type here to search in options..", &this->search_filter,
                        ImGuiInputTextFlags_EscapeClearsAll)) {
                    this->search_filter_in_lower_case = strtolower(this->search_filter);
                }
                if (!this->search_filter.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        this->search_filter.clear();
                        this->search_filter_in_lower_case.clear();
                    }
                }
                ImGui::Spacing();

                // draw all options
                auto options = games::get_options(this->games_selected_name);
                if (!this->search_filter.empty()) {
                    for (auto category : launcher::get_categories(launcher::Options::OptionsCategory::Everything)) {
                        this->build_options(
                            options,
                            category,
                            const_cast<std::string *>(&this->search_filter_in_lower_case));
                    }
                }

                ImGui::EndChild();

                // hidden options checkbox
                ImGui::Checkbox("Show Hidden Options", &this->options_show_hidden);
                if (!cfg::CONFIGURATOR_STANDALONE && this->options_dirty) {
                    ImGui::SameLine();
                    if (ImGui::Button("Restart Game")) {
                        launcher::restart();
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker("You need to restart the game to apply the changed settings.");
                }

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // did tab selection change?
        if (this->tab_selected != tab_selected_new) {
            log_misc(
                "config",
                "tab selection changed from {} to {}",
                static_cast<uint32_t>(this->tab_selected),
                static_cast<uint32_t>(tab_selected_new));

            stop_lights_test();

            this->tab_selected = tab_selected_new;
            buttons_many_active = false;
            buttons_many_index = -1;

            ImGui::CloseCurrentPopup();
        }

        // disclaimer
        // note: distribution of modified version of this software without providing source is GPLv3 license violation.
        ImGui::TextColored(
                ImVec4(1, 0.5f, 0.5f, 1.f),
                "spice2x is free & open source; if you paid money for it, you got scammed.");
        if (cfg::CONFIGURATOR_STANDALONE) {
            ImGui::SameLine();
            if (ImGui::TextLink(PROJECT_URL)) {
                launch_shell(PROJECT_URL);
            }
        }
    }

    void Config::build_buttons(const std::string &name, std::vector<Button> *buttons, int min, int max) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "%s Buttons", name.c_str());

        std::string reset_button_str = "Clear All";
        if (name == "Keypad") {
            reset_button_str = "Use Preset";
        } else if (name == "Overlay") {
            reset_button_str = "Reset All";
        }

        const float clear_w = ImGui::CalcTextSize(reset_button_str.c_str()).x
            + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - clear_w);

        std::string reset_str = "Reset all " + name + " button bindings";

        // clear all
        ImGui::PushID(name.c_str());
        if (ImGui::Button(reset_button_str.c_str())) {
            ImGui::OpenPopup(reset_str.c_str());
        }
        if (ImGui::BeginPopupModal(reset_str.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Are you sure? This can't be undone.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (name == "Keypad") {
                if (ImGui::Button("Use Numpad")) {
                    for (auto &button : *buttons) {
                        reset_button_to_default(&button, button.getVKeyDefault());
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Use Top Row")) {
                    for (auto &button : *buttons) {
                        reset_button_to_default(&button, get_keypad_top_row(button));
                    }
                    ImGui::CloseCurrentPopup();
                }                
                ImGui::SameLine();
                if (ImGui::Button("Remove All")) {
                    for (auto &button : *buttons) {
                        reset_button_to_default(&button, 0xFF);
                    }
                    ImGui::CloseCurrentPopup();
                }                
            } else {
                if (ImGui::Button("Yes")) {
                    for (auto &button : *buttons) {
                        reset_button_to_default(&button, button.getVKeyDefault());
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        ImGui::Separator();

        if (ImGui::BeginTable("ButtonsTable", 3, ImGuiTableFlags_Resizable)) {
            // longest column is probably "Toggle Virtual Keypad P1" in Overlay tab
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(220));
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(140));

            // check if empty
            if (!buttons || buttons->empty()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(INDENT);
                ImGui::TextDisabled("-");
                ImGui::Unindent(INDENT);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
            }

            // check buttons
            if (buttons) {
                const int button_it_max = max < 0 ? buttons->size() - 1 : std::min((int) buttons->size() - 1, max);
                for (int button_it = min; button_it <= button_it_max; button_it++) {

                    Button &primary_button = buttons->at(button_it);

                    // primary
                    build_button(name, primary_button, &primary_button, button_it, button_it_max, 0);

                    // alternatives
                    int alt_index = 1; // 0 is primary
                    for (auto &alt : primary_button.getAlternatives()) {
                        if (alt.isValid()) {
                            build_button(name, primary_button, &alt, button_it, button_it_max, alt_index);
                        }
                        alt_index++;
                    }
                }
            }

            ImGui::EndTable();
        }
    }

    void Config::build_button(
            const std::string &name,
            Button &primary_button,
            Button *button,
            const int button_it,
            const int button_it_max,
            const int alt_index) {

        ImGui::TableNextRow();
        set_alternating_row_colors(button_it);
        ImGui::TableNextColumn();

        // get button info
        ImGui::PushID(button);
        auto button_name = button->getName();
        if (button_name == "Service") {
            button_name = "Insert Service Coin";
        } else if (button_name == "Test") {
            button_name = "Enter Test Menu";
        } else if (button_name == "Coin Mech") {
            button_name = "Activate Coin Mech";
        }
        const auto button_display = button->getDisplayString(RI_MGR.get());
        const auto button_velocity = GameAPI::Buttons::getVelocity(RI_MGR, *button);

        // list entry

        const bool primary_button_state = GameAPI::Buttons::getState(RI_MGR, primary_button);
        const bool button_state = GameAPI::Buttons::getState(RI_MGR, *button, false);

        ImGui::Indent(INDENT);
        if (alt_index == 0) {
            // primary button
            ImGui::AlignTextToFramePadding();
            if (primary_button_state) {
                ImGui::PushStyleColor(ImGuiCol_Text, TEXT_COLOR_GREEN);
            }
            ImGui::TextTruncated(
                button_name, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
            if (primary_button_state) {
                ImGui::PopStyleColor();
            }
        } else {
            // alternate button
            ImGui::AlignTextToFramePadding();
            if (button_state) {
                ImGui::PushStyleColor(ImGuiCol_Text, TEXT_COLOR_GREEN);
            }
            ImGui::BeginDisabled();
            ImGui::TextTruncated(
                fmt::format("\u00B7 alternate #{}", alt_index),
                ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
            ImGui::EndDisabled();
            if (button_state) {
                ImGui::PopStyleColor();
            }
        }
        ImGui::Unindent(INDENT);

        // column for key binding display
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (button_state) {
            ImGui::PushStyleColor(ImGuiCol_Text, TEXT_COLOR_GREEN);
        }
        if (alt_index > 0) {
            ImGui::BeginDisabled();
            ImGui::TextTruncated(
                "\u00B7 " + button_display, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
            ImGui::EndDisabled();
        } else {
            ImGui::TextTruncated(
                button_display, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
        }
        if (button_state) {
            ImGui::PopStyleColor();
        }
        // clear button
        if (button_display.size() > 0 || alt_index > 0) {
            ImGui::SameLine();
            if (ImGui::DeleteButton(button_display.size() > 0 ? "Unbind" : "Delete")) { 
                clear_button(button, alt_index);
            }
        }

        // column for actions
        ImGui::TableNextColumn();

        // normal button binding
        std::string bind_name = "Bind " + button_name;
        if (ImGui::Button("Bind")
            || (buttons_many_active && buttons_many_active_section == name && !buttons_bind_active
                && alt_index == 0
                && !buttons_many_naive && buttons_many_index == button_it
                && ++buttons_many_delay > 25)) {
            ImGui::OpenPopup(bind_name.c_str());
            buttons_bind_active = true;
            if (buttons_many_active) {
                buttons_many_delay = 0;
                buttons_many_index = button_it;
                buttons_many_naive = false;
                buttons_many_active_section = name;
            }

            // midi freeze
            RI_MGR->devices_midi_freeze(true);

            // reset updated devices
            RI_MGR->devices_get_updated();

            // remember start values in bind data
            for (auto device : RI_MGR->devices_get()) {
                switch (device.type) {
                    case rawinput::MOUSE: {
                        memcpy(device.mouseInfo->key_states_bind,
                                device.mouseInfo->key_states,
                                sizeof(device.mouseInfo->key_states_bind));
                        break;
                    }
                    case rawinput::HID: {
                        for (size_t i = 0; i < device.hidInfo->value_states.size(); i++)
                            device.hidInfo->bind_value_states[i] = device.hidInfo->value_states[i];
                        break;
                    }
                    case rawinput::MIDI: {
                        for (size_t i = 0; i < device.midiInfo->states.size(); i++)
                            device.midiInfo->bind_states[i] = device.midiInfo->states[i];
                        for (size_t i = 0; i < device.midiInfo->controls_precision.size(); i++)
                            device.midiInfo->controls_precision_bind[i] =
                                    device.midiInfo->controls_precision[i];
                        for (size_t i = 0; i < device.midiInfo->controls_single.size(); i++)
                            device.midiInfo->controls_single_bind[i] = device.midiInfo->controls_single[i];
                        for (size_t i = 0; i < device.midiInfo->controls_onoff.size(); i++)
                            device.midiInfo->controls_onoff_bind[i] = device.midiInfo->controls_onoff[i];
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
            ImGui::HelpTooltip("Bind a button to a device using Windows RawInput API.");
        }

        bind_button_popup(bind_name, button, button_it_max, alt_index);

        // naive binding
        ImGui::SameLine();
        std::string naive_string = "Naive " + button_name;
        if (ImGui::Button("Naive")
            || (buttons_many_active && buttons_many_active_section == name && !buttons_bind_active
                && alt_index == 0
                && buttons_many_naive && buttons_many_index == button_it
                && ++buttons_many_delay > 25)) {
            ImGui::OpenPopup(naive_string.c_str());
            if (buttons_many_active) {
                buttons_many_index = button_it;
                buttons_many_naive = true;
                buttons_many_delay = 0;
                buttons_many_active_section = name;
            }

            // grab current keyboard state
            for (unsigned short int i = 0x01; i < 0xFF; i++) {
                buttons_keyboard_state[i] = GetAsyncKeyState(i) != 0;
            }
        }
        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
            ImGui::HelpTooltip(
                "Uses GetAsyncKeyState to check for any keyboard / mouse input. "
                "For best performance, Bind should be preferred, but this can be used when:\n"
                "    * you don't care about which device is used\n"
                "    * you want to use input remapping or automation software\n"
                "    * if you have NKRO issues with Bind");
        }

        naive_button_popup(naive_string, button, button_it_max, alt_index);

        // ... button
        ImGui::SameLine();
        if (ImGui::Button("..")) {
            ImGui::OpenPopup("ButtonContextMenu");
        }
        bool open_edit = false;

        // context menu for ... button
        if (ImGui::BeginPopupContextItem("ButtonContextMenu")) {

            // add
            if (ImGui::MenuItem("Add alternate")) {
                bool available = false;
                // try to find one in the list that is not bound
                for (auto &alt : primary_button.getAlternatives()) {
                    if (!alt.isValid()) {
                        alt.setTemporary(true);
                        available = true;
                        break;
                    }
                }
                // old pages UI allowed 99 alternatives, which is a little excessive
                // here, limit to 8 (1 + 7 alts) here as a reasonable upper bound
                if (!available && primary_button.getAlternatives().size() < 7) {
                    // add a new one to end of the list
                    Button temp_button(button->getName());
                    temp_button.setTemporary(true);
                    primary_button.getAlternatives().push_back(temp_button);
                    ::Config::getInstance().updateBinding(
                            games_list[games_selected], temp_button,
                            primary_button.getAlternatives().size() - 1);
                }
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                ImGui::HelpTooltip(
                    "Add an alternate binding for this button. "
                    "All of the key bindings are OR'd together.");
            }

            if (alt_index == 0) {
                // Bind Many
                if (ImGui::MenuItem("Bind (many)")) {
                    buttons_many_active = true;
                    buttons_many_delay = 0;
                    buttons_many_index = button_it;
                    buttons_many_naive = false;
                    buttons_many_active_section = name;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                    ImGui::HelpTooltip("Bind many buttons in a row, starting with this one.");
                }

                // Naive Many
                if (ImGui::MenuItem("Naive (many)")) {
                    buttons_many_active = true;
                    buttons_many_index = button_it;
                    buttons_many_naive = true;
                    buttons_many_delay = 0;
                    buttons_many_active_section = name;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                    ImGui::HelpTooltip("Naive bind many buttons in a row, starting with this one.");
                }
            }

            if (ImGui::MenuItem("Edit properties")) {
                open_edit = true;
            }
            ImGui::EndPopup();
        }

        // edit dialog
        std::string edit_name = "Edit " + button->getName();
        if (alt_index > 0) {
            edit_name += " (alternate #" + std::to_string(alt_index) + ")";
        } else {
            edit_name += " (primary)";
        }
        if (open_edit) {
            ImGui::OpenPopup(edit_name.c_str());
        }
        edit_button_popup(edit_name, button_display, button, button_velocity, alt_index);

        // clean up
        ImGui::PopID();
    }

    void Config::clear_button(Button *button, const int alt_index, std::optional<unsigned short> vKey_default) {
        button->setDeviceIdentifier("");
        if (vKey_default.has_value()) {
            button->setVKey(vKey_default.value());
        } else {
            button->setVKey(0xFF);
        }
        button->setAnalogType(BAT_NONE);
        button->setDebounceUp(0.0);
        button->setDebounceDown(0.0);
        button->setVelocityThreshold(0);
        button->setInvert(false);
        button->setLastState(GameAPI::Buttons::BUTTON_NOT_PRESSED);
        button->setLastVelocity(0);
        button->setTemporary(false);
        ::Config::getInstance().updateBinding(
                games_list[games_selected], *button,
                alt_index - 1);
    }

    void Config::reset_button_to_default(Button *button, unsigned short vKey_default) {
        // delete all alternatives
        int alt_index = 1; // 0 is primary
        for (auto &alt : button->getAlternatives()) {
            clear_button(&alt, alt_index);
            alt_index++;
        }
        // reset primary button to default
        clear_button(button, 0, vKey_default);
    }

    unsigned int Config::get_keypad_top_row(const Button &button) {
        static const std::unordered_map<std::string, unsigned short> keypad_top_row_defaults = {
            {"P1 Keypad 0", 0x30}, // 0 (top row number key)
            {"P1 Keypad 1", 0x31},
            {"P1 Keypad 2", 0x32},
            {"P1 Keypad 3", 0x33},
            {"P1 Keypad 4", 0x34},
            {"P1 Keypad 5", 0x35},
            {"P1 Keypad 6", 0x36},
            {"P1 Keypad 7", 0x37},
            {"P1 Keypad 8", 0x38},
            {"P1 Keypad 9", 0x39},
            {"P1 Keypad 00", VK_OEM_MINUS},
            {"P1 Keypad Decimal", VK_OEM_PLUS},
            {"P1 Keypad Insert Card", VK_BACK}};
        
        for (const auto &[key, value] : keypad_top_row_defaults) {
            if (button.getName() == key) {
                return value;
            }
        }
        return 0xFF;
    }

    void Config::bind_button_popup(
        const std::string &bind_name, Button *button, const int button_it_max, const int alt_index) {

        if (ImGui::BeginPopupModal(bind_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

            // modal content
            ImGui::TextUnformatted("Press any button for:");
            ImGui::Text("    %s", button->getName().c_str());
            ImGui::TextUnformatted("");
            ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Press ESC to cancel!");
            ImGui::TextUnformatted("");
            ImGui::TextUnformatted(
                "Hint: if your controller is refusing to be detected,\n"
                "      try rebooting Windows. Rare Windows bug can\n"
                "      cause this.");
            ImGui::TextUnformatted("");

            bool check_devices = true;
            if (ImGui::Button("Cancel")) {
                RI_MGR->devices_midi_freeze(false);
                buttons_bind_active = false;
                buttons_many_active = false;
                check_devices = false;
                ImGui::CloseCurrentPopup();
            }
            if (buttons_many_active) {
                ImGui::SameLine();
                if (ImGui::Button("Skip")) {
                    RI_MGR->devices_midi_freeze(false);
                    buttons_bind_active = false;
                    check_devices = false;
                    inc_buttons_many_index(button_it_max);
                    ImGui::CloseCurrentPopup();
                }
            }
            if (check_devices) {
                // iterate updated devices
                auto updated_devices = RI_MGR->devices_get_updated();
                for (auto device : updated_devices) {
                    std::lock_guard<std::mutex> lock(*device->mutex);
                    switch (device->type) {
                        case rawinput::MOUSE: {
                            auto mouse = device->mouseInfo;
                            for (size_t i = 0; i < sizeof(mouse->key_states_bind); i++) {
                                if (mouse->key_states[i] && !mouse->key_states_bind[i]
                                && !ImGui::IsAnyItemHovered()) {

                                    // bind key
                                    button->setDeviceIdentifier(device->name);
                                    button->setVKey(static_cast<unsigned short>(i));
                                    button->setAnalogType(BAT_NONE);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *button, alt_index - 1);
                                    ImGui::CloseCurrentPopup();
                                    buttons_bind_active = false;
                                    inc_buttons_many_index(button_it_max);
                                    RI_MGR->devices_midi_freeze(false);
                                    break;
                                }
                            }
                            break;
                        }
                        case rawinput::KEYBOARD: {
                            auto kb = device->keyboardInfo;
                            for (unsigned short vkey = 0; vkey < 1024; vkey++) {

                                // check if key is down
                                if (vkey != VK_NUMLOCK && kb->key_states[vkey]) {

                                    // cancel on escape key
                                    if (vkey == VK_ESCAPE) {
                                        ImGui::CloseCurrentPopup();
                                        buttons_bind_active = false;
                                        buttons_many_index = -1;
                                        buttons_many_active = false;
                                        RI_MGR->devices_midi_freeze(false);
                                        break;
                                    }

                                    // bind key
                                    button->setDeviceIdentifier(device->name);
                                    button->setVKey(vkey);
                                    button->setAnalogType(BAT_NONE);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *button, alt_index - 1);
                                    ImGui::CloseCurrentPopup();
                                    buttons_bind_active = false;
                                    inc_buttons_many_index(button_it_max);
                                    RI_MGR->devices_midi_freeze(false);
                                    break;
                                }
                            }
                            break;
                        }
                        case rawinput::HID: {
                            auto hid = device->hidInfo;

                            // ignore touchscreen and digitizer button inputs
                            // digitizer has funky stuff like "Touch Valid" "Data Valid" always held high
                            if (!rawinput::touch::is_touchscreen(device) &&
                                hid->caps.UsagePage != 0x0D) {

                                // button caps
                                auto button_states_list = &hid->button_states;
                                size_t button_index = 0;
                                for (auto &button_states : *button_states_list) {
                                    for (size_t i = 0; i < button_states.size(); i++) {

                                        // check if button is down
                                        if (button_states[i]) {

                                            // bind key
                                            button->setDeviceIdentifier(device->name);
                                            button->setVKey(static_cast<unsigned short>(button_index + i));
                                            button->setAnalogType(BAT_NONE);
                                            ::Config::getInstance().updateBinding(
                                                    games_list[games_selected], *button,
                                                    alt_index - 1);
                                            ImGui::CloseCurrentPopup();
                                            buttons_bind_active = false;
                                            inc_buttons_many_index(button_it_max);
                                            RI_MGR->devices_midi_freeze(false);
                                            break;
                                        }
                                    }
                                    button_index += button_states.size();
                                }
                            }

                            // value caps
                            auto value_states = &hid->value_states;
                            auto bind_value_states = &hid->bind_value_states;
                            auto value_names = &hid->value_caps_names;
                            for (size_t i = 0; i < value_states->size(); i++) {
                                auto &state = value_states->at(i);
                                auto &bind_state = bind_value_states->at(i);
                                auto &value_name = value_names->at(i);

                                // check for valid axis names
                                if (value_name == "X" ||
                                    value_name == "Y" ||
                                    value_name == "Rx" ||
                                    value_name == "Ry" ||
                                    value_name == "Z")
                                {
                                    // check if axis is in activation area
                                    float normalized = (state - 0.5f) * 2.f;
                                    float diff = std::fabs(state - bind_state);
                                    if (std::fabs(normalized) > 0.9f && diff > 0.1f) {
                                        auto bat = normalized > 0 ? BAT_POSITIVE : BAT_NEGATIVE;

                                        // bind value
                                        button->setDeviceIdentifier(device->name);
                                        button->setVKey(static_cast<unsigned short>(i));
                                        button->setAnalogType(bat);
                                        button->setDebounceUp(0.0);
                                        button->setDebounceDown(0.0);
                                        button->setVelocityThreshold(0);
                                        ::Config::getInstance().updateBinding(
                                                games_list[games_selected], *button,
                                                alt_index - 1);
                                        ImGui::CloseCurrentPopup();
                                        buttons_bind_active = false;
                                        inc_buttons_many_index(button_it_max);
                                        RI_MGR->devices_midi_freeze(false);
                                        break;

                                    } else if (diff > 0.3f) {
                                        bind_state = state;
                                    }
                                }

                                // hat switch
                                if (value_name == "Hat switch") {

                                    // get hat switch values
                                    ButtonAnalogType buffer[3], buffer_bind[3];
                                    Button::getHatSwitchValues(state, buffer);
                                    Button::getHatSwitchValues(bind_state, buffer_bind);

                                    // check the first entry only
                                    if (buffer[0] != BAT_NONE && buffer[0] != buffer_bind[0]) {

                                        // bind value
                                        button->setDeviceIdentifier(device->name);
                                        button->setVKey(static_cast<unsigned short>(i));
                                        button->setAnalogType(buffer[0]);
                                        button->setDebounceUp(0.0);
                                        button->setDebounceDown(0.0);
                                        button->setVelocityThreshold(0);
                                        ::Config::getInstance().updateBinding(
                                                games_list[games_selected], *button,
                                                alt_index - 1);
                                        ImGui::CloseCurrentPopup();
                                        buttons_bind_active = false;
                                        inc_buttons_many_index(button_it_max);
                                        RI_MGR->devices_midi_freeze(false);
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                        case rawinput::MIDI: {
                            auto midi = device->midiInfo;

                            // iterate all 128 notes on 16 channels
                            for (unsigned short index = 0; index < midi->states.size(); index++) {
                                bool bind_key = false;
                                if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                                    // check if note is down
                                    if (midi->states[index]) {
                                        // check if it wasn't down before
                                        if (!midi->bind_states[index]) {
                                            bind_key = true;
                                        }
                                    } else {
                                        // note was on when dialog opened, is now off
                                        midi->bind_states[index] = false;
                                    }
                                } else {
                                    const auto on = midi->v2_last_on_time[index];
                                    const auto now = get_performance_milliseconds();
                                    // if NOTE ON was received in the last 120 milliseconds, bind this note
                                    if ((now - on) < 120.0) {
                                        bind_key = true;
                                    }
                                }

                                if (bind_key) {
                                    // bind key
                                    button->setDeviceIdentifier(device->name);
                                    button->setVKey(index);
                                    button->setAnalogType(BAT_NONE);
                                    button->setDebounceUp(0.0);
                                    button->setDebounceDown(0.0);
                                    // same idea as setMidiVKey - keep velocity threshold consistent
                                    button->setVelocityThreshold(
                                        device->midiInfo->v2_velocity_threshold[button->getVKey()]);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *button,
                                            alt_index - 1);
                                    ImGui::CloseCurrentPopup();
                                    buttons_bind_active = false;
                                    inc_buttons_many_index(button_it_max);
                                    RI_MGR->devices_midi_freeze(false);
                                    break;
                                }
                            }

                            // check precision controls
                            for (unsigned short index = 0; index < midi->controls_precision.size(); index++) {
                                if (midi->controls_precision[index] > 0) {
                                    if (midi->controls_precision_bind[index] == 0) {

                                        // bind control
                                        button->setDeviceIdentifier(device->name);
                                        button->setVKey(index);
                                        button->setAnalogType(BAT_MIDI_CTRL_PRECISION);
                                        button->setDebounceUp(0.0);
                                        button->setDebounceDown(0.0);
                                        button->setVelocityThreshold(0);
                                        ::Config::getInstance().updateBinding(
                                                games_list[games_selected], *button,
                                                alt_index - 1);
                                        ImGui::CloseCurrentPopup();
                                        buttons_bind_active = false;
                                        inc_buttons_many_index(button_it_max);
                                        RI_MGR->devices_midi_freeze(false);
                                        break;
                                    }
                                } else {
                                    midi->controls_precision_bind[index] = 0;
                                }
                            }

                            // check single controls
                            for (unsigned short index = 0; index < midi->controls_single.size(); index++) {
                                if (midi->controls_single[index] > 0) {
                                    if (midi->controls_single_bind[index] == 0) {

                                        // bind control
                                        button->setDeviceIdentifier(device->name);
                                        button->setVKey(index);
                                        button->setAnalogType(BAT_MIDI_CTRL_SINGLE);
                                        button->setDebounceUp(0.0);
                                        button->setDebounceDown(0.0);
                                        button->setVelocityThreshold(0);
                                        ::Config::getInstance().updateBinding(
                                                games_list[games_selected], *button,
                                                alt_index - 1);
                                        ImGui::CloseCurrentPopup();
                                        buttons_bind_active = false;
                                        inc_buttons_many_index(button_it_max);
                                        RI_MGR->devices_midi_freeze(false);
                                        break;
                                    }
                                } else {
                                    midi->controls_single_bind[index] = 0;
                                }
                            }

                            // check on/off controls
                            for (unsigned short index = 0; index < midi->controls_onoff.size(); index++) {
                                if (midi->controls_onoff[index]) {
                                    if (!midi->controls_onoff_bind[index]) {

                                        // bind control
                                        button->setDeviceIdentifier(device->name);
                                        button->setVKey(index);
                                        button->setAnalogType(BAT_MIDI_CTRL_ONOFF);
                                        button->setDebounceUp(0.0);
                                        button->setDebounceDown(0.0);
                                        button->setVelocityThreshold(0);
                                        ::Config::getInstance().updateBinding(
                                                games_list[games_selected], *button,
                                                alt_index - 1);
                                        ImGui::CloseCurrentPopup();
                                        buttons_bind_active = false;
                                        inc_buttons_many_index(button_it_max);
                                        RI_MGR->devices_midi_freeze(false);
                                        break;
                                    }
                                } else {
                                    midi->controls_onoff_bind[index] = 0;
                                }
                            }

                            for (unsigned short ch = 0; ch < midi->pitch_bend.size(); ch++) {
                                // check pitch bend down 
                                if (midi->pitch_bend[ch] < 0) {

                                    // bind control
                                    button->setDeviceIdentifier(device->name);
                                    button->setVKey(ch);
                                    button->setAnalogType(BAT_MIDI_PITCH_DOWN);
                                    button->setDebounceUp(0.0);
                                    button->setDebounceDown(0.0);
                                    button->setVelocityThreshold(0);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *button,
                                            alt_index - 1);
                                    ImGui::CloseCurrentPopup();
                                    buttons_bind_active = false;
                                    inc_buttons_many_index(button_it_max);
                                    RI_MGR->devices_midi_freeze(false);
                                    break;
                                }

                                // check pitch bend up
                                if (midi->pitch_bend[ch] > 0) {

                                    // bind control
                                    button->setDeviceIdentifier(device->name);
                                    button->setVKey(ch);
                                    button->setAnalogType(BAT_MIDI_PITCH_UP);
                                    button->setDebounceUp(0.0);
                                    button->setDebounceDown(0.0);
                                    button->setVelocityThreshold(0);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *button,
                                            alt_index - 1);
                                    ImGui::CloseCurrentPopup();
                                    buttons_bind_active = false;
                                    inc_buttons_many_index(button_it_max);
                                    RI_MGR->devices_midi_freeze(false);
                                    break;
                                }
                            }

                            break;
                        }
                        case rawinput::PIUIO_DEVICE: {
                            auto piuio_dev = device->piuioDev;

                            // iterate all PIUIO inputs
                            for (int i = 0; i < rawinput::PIUIO::PIUIO_MAX_NUM_OF_INPUTS; i++) {

                                // check for down event
                                if (piuio_dev->IsPressed(i) && !piuio_dev->WasPressed(i)) {

                                    // bind key
                                    button->setDeviceIdentifier(device->name);
                                    button->setVKey(i);
                                    button->setAnalogType(BAT_NONE);
                                    button->setDebounceUp(0.0);
                                    button->setDebounceDown(0.0);
                                    button->setVelocityThreshold(0);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *button,
                                            alt_index - 1);
                                    ImGui::CloseCurrentPopup();
                                    buttons_bind_active = false;
                                    inc_buttons_many_index(button_it_max);
                                    RI_MGR->devices_midi_freeze(false);
                                    break;
                                }
                            }

                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            // clean up
            ImGui::EndPopup();
        }
    }

    void Config::naive_button_popup(
        const std::string &naive_string, Button *button, const int button_it_max, const int alt_index) {
        if (ImGui::BeginPopupModal(naive_string.c_str(), NULL,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            buttons_bind_active = true;

            // modal content
            ImGui::TextUnformatted("Press any button for:");
            ImGui::Text("    %s", button->getName().c_str());
            ImGui::TextUnformatted("");
            const bool escape_cancels_bind = (this->tab_selected != ConfigTab::CONFIG_TAB_OVERLAY);
            if (escape_cancels_bind) {
                ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Press ESC to cancel!");
                ImGui::TextUnformatted("");
            }
            ImGui::TextUnformatted(
                "Hint: if your remapping/automation software is\n"
                "      not detected, ensure you run the software\n"
                "      as administrator.");
            ImGui::TextUnformatted("");

            bool check_devices = true;
            if (ImGui::Button("Cancel")) {
                buttons_bind_active = false;
                buttons_many_index = -1;
                buttons_many_active = false;
                check_devices = false;
                ImGui::CloseCurrentPopup();
            }
            if (buttons_many_active) {
                ImGui::SameLine();
                if (ImGui::Button("Skip")) {
                    buttons_bind_active = false;
                    check_devices = false;
                    inc_buttons_many_index(button_it_max);
                    ImGui::CloseCurrentPopup();
                }
            }

            if (check_devices) {
                // get new keyboard state
                // these are async, and some keys generate multiple vKeys (e.g., VK_SHIFT, VK_LSHIFT)
                // use care when iterating over the result (could result in torn reads)
                bool keyboard_state_new[sizeof(buttons_keyboard_state)];
                for (size_t i = 0; i < sizeof(keyboard_state_new); i++) {
                    keyboard_state_new[i] = GetAsyncKeyState(i) != 0;
                }

                // detect key presses
                for (unsigned short int vKey = 0x01; vKey < sizeof(buttons_keyboard_state); vKey++) {

                    // ignore num lock escape sequence
                    if (vKey == VK_NUMLOCK) {
                        continue;
                    }

                    // prefer VK_LSHIFT/VK_RSHIFT, VK_LCONTROL/VK_RCONTROL, VK_LMENU/VK_RMENU
                    if (vKey == VK_SHIFT || vKey == VK_CONTROL || vKey == VK_MENU) {
                        continue;
                    }

                    // check if key is newly pressed
                    if (!(keyboard_state_new[vKey] && !buttons_keyboard_state[vKey])) {
                        continue;
                    }

                    if (ImGui::IsAnyItemHovered() && (vKey == VK_LBUTTON || vKey == VK_RBUTTON)) {
                        continue;
                    }

                    // some key is newly pressed; process it here, which will result in a new bind
                    // or a cancellation
                    if (escape_cancels_bind && vKey == VK_ESCAPE) {
                        // escape cancels out
                        buttons_many_index = -1;
                        buttons_many_active = false;
                        buttons_many_active = false;
                    } else {
                        // bind key
                        button->setDeviceIdentifier("");
                        button->setVKey(vKey);
                        button->setDebounceUp(0.0);
                        button->setDebounceDown(0.0);
                        button->setVelocityThreshold(0);
                        ::Config::getInstance().updateBinding(
                                games_list[games_selected], *button,
                                alt_index - 1);
                        inc_buttons_many_index(button_it_max);
                    }

                    buttons_bind_active = false;
                    ImGui::CloseCurrentPopup();
                    break;
                }

                // clean up
                memcpy(buttons_keyboard_state, keyboard_state_new, sizeof(buttons_keyboard_state));
            }
            ImGui::EndPopup();
        }
    }

    void Config::edit_button_popup(
        const std::string &edit_name,
        const std::string &button_display,
        Button *button,
        const float button_velocity,
        const int alt_index) {

        const auto button_state = GameAPI::Buttons::getState(RI_MGR, *button, false);
        if (ImGui::BeginPopupModal(edit_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            bool dirty = false;
            auto device = RI_MGR->devices_get(button->getDeviceIdentifier());

            // binding
            ImGui::Text("Binding");

            // combo for devices
            std::string device_desc = (device != nullptr) ? device->desc : "Empty (Naive)";
            if (ImGui::BeginCombo("Device Identifier", device_desc.c_str())) {
                if (ImGui::Selectable("Empty (Naive)", button->isNaive())) {
                    button->setDeviceIdentifier("");
                    dirty = true;
                }   
                if (button->isNaive()) {
                    ImGui::SetItemDefaultFocus();
                }
                for (auto &device : RI_MGR->devices_get()) {
                    bool selected = button->getDeviceIdentifier() == device.name.c_str();
                    const auto device_desc = fmt::format("{}##{}", device.desc, device.name);
                    if (ImGui::Selectable(device_desc.c_str(), selected)) {
                        button->setDeviceIdentifier(device.name);
                        // reset controls when switching devices
                        button->setAnalogType(ButtonAnalogType::BAT_NONE);
                        button->setVelocityThreshold(0);
                        button->setVKey(0);
                        button->setInvert(false);
                        dirty = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // analog type (only for HID)
            const auto bat = button->getAnalogType();
            if ((device != nullptr) && (device->type == rawinput::DeviceType::HID)) {
                if (ImGui::BeginCombo("HID Analog Type", ButtonAnalogTypeStr[bat])) {
                    for (int i = 0; i <= (int)ButtonAnalogType::BAT_ANY; i++) {

                        // exclude the MIDI types since they are not relevant here
                        if ((int)ButtonAnalogType::BAT_MIDI_CTRL_PRECISION <= i &&
                            i <= (int)ButtonAnalogType::BAT_MIDI_PITCH_UP) {
                            continue;
                        }

                        bool selected = (int) bat == i;
                        if (ImGui::Selectable(ButtonAnalogTypeStr[i], selected)) {
                            button->setAnalogType((ButtonAnalogType) i);
                            dirty = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::HelpMarker(
                    "Positive - tilting analog stick to + direction\n"
                    "Negative - tilting analog stick to - direction\n"
                    "Hat Direction - D-Pad direction is pressed\n"
                    "Hat Neutral - D-Pad direction is NOT pressed\n"
                    "Any Direction - analog stick has non-zero value (where zero is all the way left)"
                    );
            }

            // midi
            if ((device != nullptr) && (device->type == rawinput::DeviceType::MIDI)) {
                auto type = button->getAnalogType();
                if (ImGui::RadioButton("MIDI Note", type == ButtonAnalogType::BAT_NONE)) {
                    button->setMidiVKey(RI_MGR.get(), true, 1, 0);
                    dirty = true;
                }
                ImGui::SameLine();
                ImGui::HelpMarker("Piano key, drum pad...");
                if (ImGui::RadioButton("MIDI Continuous Control (CC)",
                        type == ButtonAnalogType::BAT_MIDI_CTRL_PRECISION ||
                        type == ButtonAnalogType::BAT_MIDI_CTRL_SINGLE ||
                        type == ButtonAnalogType::BAT_MIDI_CTRL_ONOFF)) {
                    button->setMidiVKey(RI_MGR.get(), false, 1, 0);
                    button->setVelocityThreshold(0);
                    dirty = true;
                }
                ImGui::SameLine();
                ImGui::HelpMarker("Knobs, pedals, sliders...");
                if (ImGui::RadioButton(
                        "Pitch Bend",
                        type == ButtonAnalogType::BAT_MIDI_PITCH_UP ||
                        type == ButtonAnalogType::BAT_MIDI_PITCH_DOWN)) {
                    button->setVKey(0);
                    button->setAnalogType(ButtonAnalogType::BAT_MIDI_PITCH_UP);
                    button->setVelocityThreshold(0);
                    dirty = true;
                }
                
                type = button->getAnalogType();

                int midi_channel = 0;
                int midi_index = 0;
                button->getMidiVKey(midi_channel, midi_index);
                if (type == ButtonAnalogType::BAT_NONE ||
                    type == ButtonAnalogType::BAT_MIDI_CTRL_PRECISION ||
                    type == ButtonAnalogType::BAT_MIDI_CTRL_SINGLE ||
                    type == ButtonAnalogType::BAT_MIDI_CTRL_ONOFF) {

                    // midi channel selection
                    if (ImGui::InputInt("MIDI Channel", &midi_channel, 1, 1)) {
                        // wrap around
                        if (midi_channel < 1) {
                            midi_channel = 16;
                        } else if (16 < midi_channel) {
                            midi_channel = 1;
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        dirty = true;
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "Select the MIDI channel. "
                        "Consult the manual of your instrument.\n\n"
                        "Typically, Channel 1 is the default, but Channel 10 is reserved for drums.");

                    // note or CC selection
                    if (type == ButtonAnalogType::BAT_NONE) {
                        if (ImGui::InputInt("MIDI Note #", &midi_index, 1, 8)) {
                            // notes range in [0, 127] - wrap around if outside range
                            if (128 <= midi_index) {
                                midi_index = 0;
                            } else if (midi_index < 0) {
                                midi_index = 127;
                            }
                        }
                    } else {
                        if (ImGui::InputInt("MIDI CC #", &midi_index, 1, 8)) {
                            // skip LSB range [0x20, 0x3F]
                            if (midi_index == 0x3F) {
                                midi_index = 0x1F;
                            } else if (0x20 <= midi_index && midi_index < 0x3F) {
                                midi_index = 0x40;

                            // skip range [0x60, 0x65] 
                            } else if (midi_index == 0x65) {
                                midi_index = 0x5F;
                            } else if (0x60 <= midi_index && midi_index < 0x65) {
                                midi_index = 0x66;

                            // wrap around
                            } else if (0x78 <= midi_index) {
                                midi_index = 0;
                            } else if (midi_index < 0) {
                                midi_index = 0x77;
                            }
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        dirty = true;
                    }
                    if (type != ButtonAnalogType::BAT_NONE) {
                        ImGui::TextDisabled("Valid CC: 0-31, 64-95, 102-119");
                    }
                    if (dirty) {
                        button->setMidiVKey(
                            RI_MGR.get(),
                            (type == ButtonAnalogType::BAT_NONE),
                            midi_channel, midi_index);
                    }

                    if (type == ButtonAnalogType::BAT_NONE) {
                        // when we called GameAPI::Buttons::getState(...) above (possibly for the first time)
                        // at the api level we would have retrieved the software threshold from Button and
                        // applied it to the rawinput MIDI layer already
                        // therefore, initially retrieve the value from rawinput, but save to both if changed
                        int velocity_threshold = device->midiInfo->v2_velocity_threshold[button->getVKey()];
                        ImGui::TextUnformatted("\n");
                        ImGui::AlignTextToFramePadding();
                        if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                            ImGui::TextDisabled("Velocity Threshold (Disabled)");
                            ImGui::SameLine();
                            ImGui::HelpMarker("Disabled because legacy MIDI algorithm is in use.");
                        } else {
                            ImGui::TextUnformatted("Velocity Threshold");
                            ImGui::SameLine();
                            ImGui::WarnMarker(
                                "Ignore Note-On events if velocity is below this threshold. "
                                "Helpful for preventing misfires on sensitive drum pads.",
                                "This value is set per MIDI note, not per game button. This means that if "
                                "you have multiple bindings to the same MIDI note, they will share the "
                                "same threshold value.");
                        }

                        ImGui::Separator();
                        if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                            ImGui::BeginDisabled();
                            int velocity_threshold_fake = 0;
                            ImGui::SliderInt(
                                "",
                                &velocity_threshold_fake,
                                0,
                                0x7f,
                                "%d/127",
                                ImGuiSliderFlags_AlwaysClamp);
                            ImGui::EndDisabled();
                        } else {
                            if (ImGui::SliderInt(
                                    "",
                                    &velocity_threshold,
                                    0,
                                    0x7f,
                                    "%d/127",
                                    ImGuiSliderFlags_AlwaysClamp)) {
                                // set in the binding...
                                button->setVelocityThreshold(velocity_threshold);
                                // ... then set on the device as well for rawinput to pick up
                                device->midiInfo->v2_velocity_threshold_set_on_device[button->getVKey()] = true;
                                device->midiInfo->v2_velocity_threshold[button->getVKey()] = velocity_threshold;
                            }
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            dirty = true;
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Min. %d%%", velocity_threshold * 100 / 127);

                    } else if (type == ButtonAnalogType::BAT_MIDI_CTRL_PRECISION ||
                            type == ButtonAnalogType::BAT_MIDI_CTRL_SINGLE) { 

                        ImGui::TextUnformatted("\n");
                        ImGui::AlignTextToFramePadding();
                        if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                            ImGui::TextDisabled("Threshold (Disabled)");
                            ImGui::SameLine();
                            ImGui::HelpMarker("Disabled because legacy MIDI algorithm is in use.");
                        } else {
                            ImGui::TextUnformatted("Threshold");
                            ImGui::SameLine();
                            ImGui::HelpMarker(
                                "Value from CC must be greater than this value to be considered on. "
                                "For example, if this is a hi-hat pedal, how far you have to press down.");
                        }

                        ImGui::Separator();
                        int threshold = button->getVelocityThreshold();
                        if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                            ImGui::BeginDisabled();
                            int threshold_fake = 0;
                            ImGui::SliderInt(
                                "",
                                &threshold_fake,
                                0,
                                0x7f,
                                "%d/127",
                                ImGuiSliderFlags_AlwaysClamp);
                            ImGui::EndDisabled();
                        } else {
                            if (ImGui::SliderInt(
                                    "",
                                    &threshold,
                                    0,
                                    0x7f,
                                    "%d/127",
                                    ImGuiSliderFlags_AlwaysClamp)) {
                                button->setVelocityThreshold(threshold);
                            }
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            dirty = true;
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Min. %d%%", threshold * 100 / 127);
                    }

                } else if (type == ButtonAnalogType::BAT_MIDI_PITCH_DOWN ||
                    type == ButtonAnalogType::BAT_MIDI_PITCH_UP) {

                    if (ImGui::InputInt("MIDI Channel", &midi_channel, 1, 1)) {
                        // wrap around
                        if (midi_channel < 1) {
                            midi_channel = 16;
                        } else if (16 < midi_channel) {
                            midi_channel = 1;
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        button->setVKey(midi_channel - 1);
                        dirty = true;
                    }

                    const bool is_up = type == ButtonAnalogType::BAT_MIDI_PITCH_UP;
                    if (ImGui::BeginCombo(
                            "Direction",
                            is_up ? "Pitch Up" : "Pitch Down",
                            ImGuiComboFlags_HeightSmall)) {
                        
                        if (ImGui::Selectable("Pitch Up", is_up)) {
                            button->setAnalogType(ButtonAnalogType::BAT_MIDI_PITCH_UP);
                            dirty = true;
                        }
                        if (is_up) {
                            ImGui::SetItemDefaultFocus();
                        }
                        if (ImGui::Selectable("Pitch Down", !is_up)) {
                            button->setAnalogType(ButtonAnalogType::BAT_MIDI_PITCH_DOWN);
                            dirty = true;
                        }
                        if (!is_up) {
                            ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

            } else {
                int vKey = button->getVKey();
                if (ImGui::InputInt(button->isNaive() ? "Virtual Key" : "Index", &vKey, 1, 1)) {
                    button->setVKey(vKey);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    dirty = true;
                }
            }

            // preview
            if (!button_display.empty()) {
                ImGui::TextUnformatted("\nPreview");
                ImGui::Separator();
                if (button_state == GameAPI::Buttons::State::BUTTON_PRESSED) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.7f, 0.f, 1.f));
                }
                // this doesn't account for utf-8 but whatever
                if (button_display.size() >= 40) {
                    ImGui::Text("%.37s...", button_display.c_str());
                    ImGui::SameLine();
                    ImGui::HelpMarker(button_display.c_str());
                } else {
                    ImGui::TextUnformatted(button_display.c_str());
                }
                ImGui::TextUnformatted("\n");
                if (button_state == GameAPI::Buttons::State::BUTTON_PRESSED) {
                    ImGui::PopStyleColor();
                }
            } else {
                ImGui::TextUnformatted("");
            }

            // options
            ImGui::Text("Options");

            // check for debounce
            if (button->getDebounceUp() || button->getDebounceDown()
                || (device != nullptr && (
                    device->type == rawinput::MOUSE ||
                    device->type == rawinput::KEYBOARD ||
                    (device->type == rawinput::HID && button->getAnalogType() == BAT_NONE)
                ))) {

                // debounce up
                auto debounce_up = button->getDebounceUp() * 1000;
                if (ImGui::InputDouble("Debounce Up (ms)", &debounce_up, 1, 1, "%.2f")) {
                    debounce_up = std::max(0.0, debounce_up);
                    button->setDebounceUp(debounce_up * 0.001);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    dirty = true;
                }
                ImGui::SameLine();
                ImGui::HelpMarker("Time a button needs to be up to be detected as up.\n"
                                "Can solve micro switch issues with long notes for example.");

                // debounce down
                auto debounce_down = button->getDebounceDown() * 1000;
                if (ImGui::InputDouble("Debounce Down (ms)", &debounce_down, 1, 1, "%.2f")) {
                    debounce_down = std::max(0.0, debounce_down);
                    button->setDebounceDown(debounce_down * 0.001);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    dirty = true;
                }
                ImGui::SameLine();
                ImGui::HelpMarker("Time a button needs to be down to be detected as down.\n"
                                "This setting will add noticable input lag.");
            }

            // invert
            bool invert = button->getInvert();
            if (ImGui::Checkbox("Invert", &invert)) {
                button->setInvert(invert);
                dirty = true;
            }

            // state display
            ImGui::TextUnformatted("");
            if (device != nullptr && device->type == rawinput::MIDI) {
                ImGui::Text("Button State");
                ImGui::SameLine();
                ImGui::HelpMarker(
                    "When interpreted as digital input; for most games including GITADORA\n\n"
                    "This is just a preview; each game's I/O engine will behave differently (such as poll rate) "
                    "so make sure you test your input in the game's test menu"
                    );
                ImGui::ProgressBar(
                    button_state == GameAPI::Buttons::State::BUTTON_PRESSED ? 1.0f : 0.f);
                ImGui::Text("Velocity");
                ImGui::SameLine();
                ImGui::HelpMarker(
                    "When interpreted as velocity-sensitive input, such as Nostalgia\n\n"
                    "This is just a preview; each game's I/O engine will behave differently (such as poll rate) "
                    "so make sure you test your input in the game's test menu"
                    );
                ImGui::ProgressBar(button_velocity);
            } else {
                ImGui::Text("State");
                ImGui::ProgressBar(button_velocity);
            }

            // check if dirty
            if (dirty) {
                ::Config::getInstance().updateBinding(
                        games_list[games_selected], *button, alt_index - 1);
            }

            // close button
            ImGui::TextUnformatted("");
            if (ImGui::Button("Save & Close")) {
                buttons_many_active = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void Config::build_analogs(const std::string &name, std::vector<Analog> *analogs) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Analogs");

        const float clear_w = ImGui::CalcTextSize("Clear All").x
            + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - clear_w);

        // clear all
        if (ImGui::Button("Clear All")) {
            ImGui::OpenPopup("Clear all analog bindings");
        }
        if (ImGui::BeginPopupModal("Clear all analog bindings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Are you sure? This can't be undone.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("Yes")) {
                for (auto &analog : *analogs) {
                    // explicitly not checking for analog.isSet() here
                    // since it doesn't account for a binding with valid device but invalid index
                    analog.clearBindings();
                    ::Config::getInstance().updateBinding(games_list[games_selected], analog);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        if (ImGui::BeginTable("AnalogsTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {

            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(220));
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(100));

            // check if empty
            if (!analogs || analogs->empty()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(INDENT);
                ImGui::TextDisabled("-");
                ImGui::Unindent(INDENT);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
            }

            // check analogs
            if (analogs) {
                for (auto &analog : *analogs) {

                    // get analog info
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(&analog);

                    auto analog_name = analog.getName();
                    auto analog_display = analog.getDisplayString(RI_MGR.get());
                    auto analog_state = GameAPI::Analogs::getState(RI_MGR, analog);
                    analog_state = std::clamp(analog_state, 0.f, 1.f);

                    // list entry
                    ImGui::ProgressBar(analog_state, ImVec2(32.f, 0));
                    ImGui::SameLine();
                    if (analog_display.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.f));
                    }
                    ImGui::TextTruncated(
                        analog_name, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextTruncated(
                        analog_display, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));

                    // clear analog
                    if (analog_display.size() > 0) {
                        ImGui::SameLine();
                        if (ImGui::DeleteButton("Remove")) {
                            analog.clearBindings();
                            ::Config::getInstance().updateBinding(
                                    games_list[games_selected], analog);
                        }
                    }

                    ImGui::TableNextColumn();
                    if (analog_display.empty()) {
                        ImGui::PopStyleColor();
                    }

                    // analog binding
                    if (ImGui::Button("Set")) {
                        ImGui::OpenPopup("Analog Binding");

                        // get devices
                        this->analogs_devices.clear();
                        for (auto &device : RI_MGR->devices_get()) {
                            switch (device.type) {
                                case rawinput::MOUSE:
                                    this->analogs_devices.emplace_back(&device);
                                    break;
                                case rawinput::HID:
                                    if (!device.hidInfo->value_caps_names.empty())
                                        this->analogs_devices.emplace_back(&device);
                                    break;
                                case rawinput::MIDI:
                                    this->analogs_devices.emplace_back(&device);
                                    break;
                                default:
                                    continue;
                            }

                            // check if this is the current device
                            if (device.name == analog.getDeviceIdentifier()) {
                                analogs_devices_selected = this->analogs_devices.size() - 1;
                            }
                        }
                    }

                    edit_analog_popup(analog);

                    // clean up
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }

    void Config::edit_analog_popup(Analog &analog) {
        if (ImGui::BeginPopupModal("Analog Binding", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

            // device selector
            auto analog_device_changed = ImGui::Combo(
                "Device",
                &this->analogs_devices_selected,
                [](void* data, int i, const char **item) {
                    *item = ((std::vector<rawinput::Device*>*) data)->at(i)->desc.c_str();
                    return true;
                },
                &this->analogs_devices, (int) this->analogs_devices.size());

            // obtain controls
            std::vector<std::string> control_names;
            std::vector<int> analogs_midi_indices;
            if (this->analogs_devices_selected >= 0) {
                auto device = this->analogs_devices.at(this->analogs_devices_selected);

                // add a tooltip to devices selector
                if (!device->name.empty()) {
                    ImGui::SameLine();
                    ImGui::HelpMarker(device->name.c_str());
                }
                
                switch (device->type) {
                    case rawinput::MOUSE: {

                        // add X/Y axis and mouse wheel
                        control_names.push_back("X");
                        control_names.push_back("Y");
                        control_names.push_back("Scroll Wheel");
                        break;
                    }
                    case rawinput::HID: {

                        // add value names
                        for (auto &analog_name : device->hidInfo->value_caps_names) {
                            control_names.push_back(analog_name);
                        }
                        break;
                    }
                    case rawinput::MIDI: {

                        // add precision values, if they have been detected since launch
                        auto precision = device->midiInfo->controls_precision;
                        for (size_t i = 0; i < precision.size(); i++) {
                            if (device->midiInfo->controls_precision_set[i]) {
                                const int channel = (i / 32) + 1;
                                const int cc_index = (i % 32);
                                control_names.push_back(
                                    fmt::format("Prec Ctrl Ch.{} CC#{}", channel, cc_index)
                                );
                                analogs_midi_indices.push_back(i);
                            }
                        }

                        // add single values, if they have been detected since launch
                        auto single = device->midiInfo->controls_single;
                        for (size_t i = 0; i < single.size(); i++) {
                            if (device->midiInfo->controls_single_set[i]) {
                                const int channel = (i / 44) + 1;
                                int cc_index = (i % 44);
                                if (cc_index < 26) {
                                    cc_index += 0x46; // single byte range
                                } else {
                                    cc_index = cc_index - 26 + 0x66; // undefined single byte range
                                }
                                control_names.push_back(
                                    fmt::format("Ctrl Ch.{} CC#{}", channel, cc_index)
                                );
                                analogs_midi_indices.push_back(i + precision.size());
                            }
                        }

                        // add onoff values, if they have been detected since launch
                        auto onoff = device->midiInfo->controls_onoff;
                        for (size_t i = 0; i < onoff.size(); i++) {
                            if (device->midiInfo->controls_onoff_set[i]) {
                                const int channel = (i / 6) + 1;
                                const int cc_index = (i % 6) + 0x40;
                                control_names.push_back(
                                    fmt::format("OnOff Ch.{} CC#{}", channel, cc_index)
                                );
                                analogs_midi_indices.push_back(i + precision.size() + single.size());
                            }
                        }

                        // add pitch bend, if they have been detected since launch
                        auto pitch = device->midiInfo->pitch_bend;
                        for (size_t ch = 0; ch < pitch.size(); ch++) {
                            if (device->midiInfo->pitch_bend_set[ch]) {
                                control_names.push_back(fmt::format("Pitch Ch.{}", ch + 1));
                                analogs_midi_indices.push_back(
                                    ch + 
                                    precision.size() +
                                    single.size() +
                                    onoff.size());
                            }
                        }
                    }
                    default:
                        break;
                }

                // select the previously chosen value
                auto selected_control = 0;
                if (!analog_device_changed) {
                    if (analogs_midi_indices.empty()) {
                        selected_control = analog.getIndex();
                    } else {
                        for (size_t i = 0; i < analogs_midi_indices.size(); i++) {
                            if (analog.getIndex() == analogs_midi_indices.at(i)) {
                                selected_control = i;
                                break;
                            }
                        }
                    }

                    if (0 <= selected_control && selected_control < static_cast<int>(control_names.size())) {
                        this->analogs_devices_control_selected = selected_control;
                    }
                }
            }

            // controls
            ImGui::Combo("Control",
                        &this->analogs_devices_control_selected,
                        [](void* data, int i, const char **item) {
                            *item = ((std::vector<std::string>*) data)->at(i).c_str();
                            return true;
                        },
                        &control_names, control_names.size());

            // multiplier/sensitivity/deadzone
            if (this->analogs_devices_selected >= 0) {
                auto device = this->analogs_devices.at(this->analogs_devices_selected);

                if (device->type == rawinput::MIDI) {
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "From the MIDI specification:\n"
                        "  Prec Ctrl = Double precision control (CC 0-31)\n"
                        "  OnOff = On or Off control, pedals (CC 64-69)\n"
                        "  Ctrl = Single precision control (CC 70-95, 102-119)\n"
                        );
                    ImGui::TextDisabled(
                        "Hint: don't see MIDI controls in drop down?\n"
                        "Use your knob/slider/pedal and try again.");
                }

                if (device->type == rawinput::HID) {
                    auto multiplier = analog.getMultiplier();
                    int multiplier_index = 7; // 1:1
                    if (multiplier < -1) {
                        // turn -8 into [0], -2 into [6], and so on
                        multiplier_index = multiplier + 8;
                    } else if (1 < multiplier) {
                        // turn 2 into [8], 8 into [14], etc
                        multiplier_index = multiplier + 6;
                    }
                    const bool value_changed = ImGui::Combo(
                        "Multiplier",
                        &multiplier_index,
                        "1/8\0" // [0]
                        "1/7\0" // [1]
                        "1/6\0" // [2]
                        "1/5\0" // [3]
                        "1/4\0" // [4]
                        "1/3\0" // [5]
                        "1/2\0" // [6]
                        "1:1\0" // [7] <-- default
                        "2x\0"  // [8]
                        "3x\0"  // [9]
                        "4x\0"  // [10]
                        "5x\0"  // [11]
                        "6x\0"  // [12]
                        "7x\0"  // [13]
                        "8x\0"  // [14]
                        "\0");

                    ImGui::SameLine();
                    ImGui::HelpMarker("Apply a static integer multiplier or divisor to the input.");
                    if (value_changed) {
                        if (multiplier_index < 7) {
                            // turn [0] into -8, [6] to -2, etc
                            multiplier = multiplier_index - 8;
                        } else if (7 < multiplier_index) {
                            // turn [8] to 2, [14] to 8, etc
                            multiplier = multiplier_index - 6;
                        } else {
                            multiplier = 1;
                        }
                        analog.setMultiplier(multiplier);
                    }
                }
                if (device->type == rawinput::MOUSE || device->type == rawinput::HID) {
                    auto sensitivity = sqrtf(analog.getSensitivity());
                    const bool value_changed =
                        ImGui::SliderFloat("Sensitivity", &sensitivity, 0.f, 2.f, "%.3f");
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "Adjust floating point multiplier to relative movement.\n\n"
                        "Value is squared before being multiplied (e.g., 1.44 is 2x sensitivity, 2.00 is 4x).\n\n"
                        "Dependent on how often the game polls for input. Intended for angular input (knobs, turntables)");
                    if (value_changed) {
                        analog.setSensitivity(sensitivity * sensitivity);
                    }
                }
                if (device->type == rawinput::HID || device->type == rawinput::MIDI) {
                    auto deadzone = analog.getDeadzone();
                    const bool value_changed =
                        ImGui::SliderFloat("Deadzone", &deadzone, -0.999f, 0.999f, "%.3f");
                    if (value_changed) {
                        analog.setDeadzone(deadzone);
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker("Positive values specify a deadzone around the middle.\n"
                                    "Negative values specify a deadzone from the minimum value.");

                    // deadzone mirror
                    bool deadzone_mirror = analog.getDeadzoneMirror();
                    ImGui::Checkbox("Deadzone Mirror", &deadzone_mirror);
                    ImGui::SameLine();
                    ImGui::HelpMarker("Positive deadzone values cut off at edges instead.\n"
                                    "Negative deadzone values cut off at maximum value instead.");
                    if (deadzone_mirror != analog.getDeadzoneMirror()) {
                        analog.setDeadzoneMirror(deadzone_mirror);
                    }
                }
            }

            // invert axis
            bool invert = analog.getInvert();
            ImGui::Checkbox("Invert Axis", &invert);
            ImGui::SameLine();
            ImGui::HelpMarker("Flip the direction of analog input.");
            if (invert != analog.getInvert()) {
                analog.setInvert(invert);
            }
            
            if (this->analogs_devices_selected >= 0) {
                const auto device = this->analogs_devices.at(this->analogs_devices_selected);
                if (device->type == rawinput::HID) {
                    // smoothing
                    bool smoothing = analog.getSmoothing();
                    ImGui::BeginDisabled(analog.isRelativeMode());
                    ImGui::Checkbox("Smooth Axis (adds latency)", &smoothing);
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "Apply a moving average algorithm; intended for angular input (knobs, turntables). "
                        "Adds a slight bit of latency to input as the algorithm averages out recent input. "
                        "Only use in dire situations where the input is too jittery for the game.");
                    ImGui::EndDisabled();
                    if (smoothing != analog.getSmoothing()) {
                        analog.setSmoothing(smoothing);
                    }

                    // relative input mode
                    bool relative_analog = analog.isRelativeMode();
                    ImGui::Checkbox("Relative Axis (experimental)", &relative_analog);
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "Use relative directional input instead of positional values.\n\n"
                        "Can be used to translate analog sticks to knob input, for example.\n\n"
                        "WARNING: speed depends on how often the game polls for input! "
                        "Strongly recommended that you go into the game's test menu instead "
                        "of adjusting in spicecfg.");
                    if (relative_analog != analog.isRelativeMode()) {
                        analog.setRelativeMode(relative_analog);
                    }

                    // delay buffer
                    int delay = analog.getDelayBufferDepth();
                    ImGui::InputInt("Delay (experimental)", &delay, 1, 10);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        delay = CLAMP(delay, 0, 256);
                        analog.setDelayBufferDepth(delay);
                    }
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "Adds a delay to input. This is poll-based, not time-based.\n\n"
                        "WARNING: delay depends on how often the game polls for input! "
                        "Strongly recommended that you go into the game's test menu instead "
                        "of adjusting in spicecfg.");
                }
            }

            // current state
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0.f, 1.f), "Preview");
            float value = GameAPI::Analogs::getState(RI_MGR, analog);
            ImGui::ProgressBar(value);

            // centered knob preview
            const float knob_size = 64.f;
            auto width = ImGui::GetContentRegionAvail().x - knob_size;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width / 2));
            ImGui::Knob(value, knob_size);

            // update analog
            if (analogs_devices_selected >= 0 && analogs_devices_selected < (int) analogs_devices.size()) {

                // update identifier on change
                auto identifier = this->analogs_devices.at(this->analogs_devices_selected)->name;
                if (identifier != analog.getDeviceIdentifier()) {
                    analog.setDeviceIdentifier(identifier);
                }

                // update control
                if (this->analogs_devices_control_selected >= 0) {

                    // MIDI devices have their own dynamic indices
                    auto index = this->analogs_devices_control_selected;
                    if (!analogs_midi_indices.empty()) {
                        if (this->analogs_devices_control_selected < (int) analogs_midi_indices.size()) {
                            index = analogs_midi_indices[this->analogs_devices_control_selected];
                        }
                    }

                    // update index on change
                    if ((int) analog.getIndex() != index) {
                        analog.setIndex(index);
                    }
                }
            }

            // close button
            ImGui::Separator();
            if (ImGui::Button("Save & Close")) {
                ::Config::getInstance().updateBinding(games_list[games_selected], analog);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                analog.resetValues();
            }

            // clean up
            ImGui::EndPopup();
        }
    }

    void Config::stop_lights_test() {
        if (!lights_testing) {
            return;
        }
        auto *lights = games::get_lights(this->games_selected_name);
        if (lights) {
            std::vector<int> bound;
            for (int i = 0; i < (int) lights->size(); i++) {
                if ((*lights)[i].isSet()) {
                    bound.push_back(i);
                }
            }
            if (lights_test_current >= 0 && lights_test_current < (int) bound.size()) {
                auto &cur = (*lights)[bound[lights_test_current]];
                GameAPI::Lights::writeLight(RI_MGR, cur, 0.f);
                RI_MGR->devices_flush_output();
            }
        }
        lights_testing = false;
        lights_test_current = -1;
    }

    void Config::update() {
        Window::update();
        if (lights_testing && !this->active) {
            stop_lights_test();
        }
    }

    void Config::build_lights(const std::string &name, std::vector<Light> *lights) {
        if (lights && !lights->empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Lights");

            // auto match popup cleanup
            if (auto_match_testing && !ImGui::IsPopupOpen("Auto Match Lights")) {
                if (!auto_match_test_device.empty()) {
                    Light temp("cleanup");
                    temp.setDeviceIdentifier(auto_match_test_device);
                    temp.setIndex(auto_match_test_control);
                    GameAPI::Lights::writeLight(RI_MGR, temp, 0.f);
                    RI_MGR->devices_flush_output();
                }
                auto_match_testing = false;
                auto_match_test_current = -1;
                auto_match_test_device.clear();
            }

            // bound lights for test all
            std::vector<int> bound;
            for (int i = 0; i < (int) lights->size(); i++) {
                if ((*lights)[i].isSet()) {
                    bound.push_back(i);
                }
            }

            // test all sequence
            if (lights_testing && !bound.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lights_test_time).count();
                if (elapsed >= 500) {
                    if (lights_test_current >= 0 && lights_test_current < (int) bound.size()) {
                        auto &prev = (*lights)[bound[lights_test_current]];
                        GameAPI::Lights::writeLight(RI_MGR, prev, 0.f);
                        RI_MGR->devices_flush_output();
                    }
                    lights_test_current++;
                    if (lights_test_current >= (int) bound.size()) {
                        lights_test_current = 0;
                    }
                    lights_test_time = now;
                    auto &next = (*lights)[bound[lights_test_current]];
                    GameAPI::Lights::writeLight(RI_MGR, next, 1.f);
                    RI_MGR->devices_flush_output();
                }
            }

            // button row
            const char *test_label = lights_testing ? "Stop Testing" : "Test All";
            float match_w = ImGui::CalcTextSize("Auto Match Lights").x
                + ImGui::GetStyle().FramePadding.x * 2;
            float test_w = ImGui::CalcTextSize(test_label).x
                + ImGui::GetStyle().FramePadding.x * 2;
            float clear_w = ImGui::CalcTextSize("Clear All").x
                + ImGui::GetStyle().FramePadding.x * 2;
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float total_w = clear_w + spacing + test_w + spacing + match_w;
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                + ImGui::GetContentRegionAvail().x - total_w);

            // clear all
            ImGui::BeginDisabled(bound.empty());
            if (ImGui::Button("Clear All")) {
                ImGui::OpenPopup("Clear all light bindings");
            }
            ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("Clear all light bindings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Are you sure? This can't be undone.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Yes")) {
                    if (lights_testing) {
                        if (lights_test_current >= 0 && lights_test_current < (int) bound.size()) {
                            auto &cur = (*lights)[bound[lights_test_current]];
                            GameAPI::Lights::writeLight(RI_MGR, cur, 0.f);
                            RI_MGR->devices_flush_output();
                        }
                        lights_testing = false;
                        lights_test_current = -1;
                    }
                    for (auto &light : *lights) {
                        if (!light.isSet()) {
                            continue;
                        }
                        clear_light(&light, 0);
                        for (int ai = 0; ai < (int) light.getAlternatives().size(); ai++) {
                            clear_light(&light.getAlternatives()[ai], ai + 1);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // test all
            ImGui::SameLine();
            ImGui::BeginDisabled(!lights_testing && bound.empty());
            if (ImGui::Button(test_label)) {
                if (lights_testing) {
                    if (lights_test_current >= 0 && lights_test_current < (int) bound.size()) {
                        auto &cur = (*lights)[bound[lights_test_current]];
                        GameAPI::Lights::writeLight(RI_MGR, cur, 0.f);
                        RI_MGR->devices_flush_output();
                    }
                    lights_testing = false;
                    lights_test_current = -1;
                } else {
                    lights_testing = true;
                    lights_test_current = 0;
                    lights_test_time = std::chrono::steady_clock::now();
                    auto &first = (*lights)[bound[0]];
                    GameAPI::Lights::writeLight(RI_MGR, first, 1.f);
                    RI_MGR->devices_flush_output();
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                ImGui::SetTooltip("Cycle through bound lights sequentially.");
            }

            // auto match lights
            ImGui::SameLine();
            if (ImGui::Button("Auto Match Lights")) {
                ImGui::OpenPopup("Auto Match Lights");

                if (lights_testing) {
                    if (lights_test_current >= 0 && lights_test_current < (int) bound.size()) {
                        auto &cur = (*lights)[bound[lights_test_current]];
                        GameAPI::Lights::writeLight(RI_MGR, cur, 0.f);
                        RI_MGR->devices_flush_output();
                    }
                    lights_testing = false;
                    lights_test_current = -1;
                }

                this->auto_match_devices.clear();
                this->auto_match_device_selected = -1;
                this->auto_match_testing = false;
                this->auto_match_test_current = -1;
                this->auto_match_test_device.clear();
                this->auto_match_soft_enabled = true;
                this->auto_match_copied = false;
                this->auto_match_p2 = false;

                for (auto &device : RI_MGR->devices_get()) {
                    switch (device.type) {
                        case rawinput::HID:
                            if (!device.hidInfo->button_output_caps_list.empty()
                                || !device.hidInfo->value_output_caps_list.empty()) {
                                this->auto_match_devices.emplace_back(&device);
                            }
                            break;
                        case rawinput::SEXTET_OUTPUT:
                        case rawinput::PIUIO_DEVICE:
                        case rawinput::SMX_STAGE:
                        case rawinput::SMX_DEDICAB:
                            this->auto_match_devices.emplace_back(&device);
                            break;
                        default:
                            continue;
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Auto match game lights to device outputs by name.");
            }

            auto_match_lights_popup(lights);
        } else {
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Lights");
        }
        ImGui::Spacing();

        auto begin_lights_table = [&]() -> bool {
            if (ImGui::BeginTable("LightsTable", 3, ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                    overlay::apply_scaling(140));
                return true;
            }
            return false;
        };

        // check if empty
        if (!lights || lights->empty()) {
            if (begin_lights_table()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(INDENT);
                ImGui::TextDisabled("-");
                ImGui::Unindent(INDENT);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
                ImGui::EndTable();
            }
            return;
        }

        // render a section header
        auto render_section_header = [](const std::string &name) {
            const float pad = ImGui::GetTextLineHeight() * 0.5f;
            ImGui::Dummy(ImVec2(0, pad));
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0.f, 1.f), "%s", name.c_str());
            ImGui::Separator();
        };

        // need to ensure a irst table begins, so current_section must not equal any existing category name
        std::string current_section = "!!invalid!!";
        bool table_begin = false;
        for (size_t i = 0; i < lights->size(); i++) {
            auto &light = lights->at(i);
            
            if (current_section != light.getCategory()) {
                current_section = light.getCategory();
                if (table_begin) {
                    ImGui::EndTable();
                }

                render_section_header(current_section.empty() ? "Uncategorized" : current_section);
                
                table_begin = begin_lights_table();
                if (!table_begin) {
                    break;
                }
            }

            build_light(light, &light, i, 0);
            int alt_index = 1;
            for (auto &alt : light.getAlternatives()) {
                if (alt.isValid()) {
                    build_light(light, &alt, i, alt_index);
                }
                alt_index++;
            }
        }
        if (table_begin) {
            ImGui::EndTable();
        }
    }

    void Config::build_light(
        Light &primary_light, Light *light, const int light_index, const int alt_index) {
        // get light info
        auto light_name = light->getName();
        auto light_display = light->getDisplayString(RI_MGR.get());
        auto light_state = GameAPI::Lights::readLight(RI_MGR, primary_light);

        // list entry
        ImGui::PushID(light);
        ImGui::TableNextRow();
        set_alternating_row_colors(light_index);

        // progress bar
        ImGui::TableNextColumn();
        
        // light name
        if (alt_index == 0) {
            ImGui::ProgressBar(light_state, ImVec2(32.f, 0));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextTruncated(
                light_name, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::Dummy(ImVec2(32.f, 0));
            ImGui::SameLine();
            ImGui::TextDisabled("\u00B7 alternate #%d", alt_index);
        }

        // binding name
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (alt_index > 0) {
            ImGui::BeginDisabled(alt_index > 0);
            ImGui::TextTruncated(
                "\u00B7 " + light_display, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
            ImGui::EndDisabled();
        } else {
            ImGui::TextTruncated(
                light_display, ImGui::GetContentRegionAvail().x - overlay::apply_scaling(20));
        }

        // clear light
        if (light_display.size() > 0 || alt_index > 0) {
            ImGui::SameLine();
            if (ImGui::DeleteButton(light_display.size() > 0 ? "Unbind" : "Delete")) { 
                clear_light(light, alt_index);
            }
        }

        // bind button
        ImGui::TableNextColumn();

        // light binding
        if (ImGui::Button("Set")) {
            ImGui::OpenPopup("Light Binding");
            light->override_enabled = true;

            // get devices
            this->lights_devices.clear();
            for (auto &device : RI_MGR->devices_get()) {
                switch (device.type) {
                    case rawinput::HID:
                        if (!device.hidInfo->button_output_caps_list.empty()
                            || !device.hidInfo->value_output_caps_list.empty())
                            this->lights_devices.emplace_back(&device);
                        break;
                    case rawinput::SEXTET_OUTPUT:
                    case rawinput::PIUIO_DEVICE:
                    case rawinput::SMX_STAGE:
                    case rawinput::SMX_DEDICAB:
                        this->lights_devices.emplace_back(&device);
                        break;
                    default:
                        continue;
                }

                // check if this is the current device
                if (device.name == light->getDeviceIdentifier()) {
                    this->lights_devices_selected = (int) this->lights_devices.size() - 1;
                    this->lights_devices_control_selected = light->getIndex();
                }
            }
        }

        edit_light_popup(primary_light, light, alt_index);

        ImGui::SameLine();
        if (alt_index == 0) {
            if (ImGui::Button("Add alt")) {
                bool available = false;
                // try to find one in the list that is not bound
                for (auto &alt : primary_light.getAlternatives()) {
                    if (!alt.isValid()) {
                        alt.setTemporary(true);
                        available = true;
                        break;
                    }
                }
                // old pages UI allowed 99 alternatives, which is a little excessive
                // here, limit to 16 (1 + 15 alts) here as a reasonable upper bound
                if (!available && primary_light.getAlternatives().size() < 7) {
                    // add a new one to end of the list
                    Light temp_light(light->getName());
                    temp_light.setTemporary(true);
                    primary_light.getAlternatives().push_back(temp_light);
                    ::Config::getInstance().updateBinding(
                            games_list[games_selected], temp_light,
                            primary_light.getAlternatives().size() - 1);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Add additional output for this light.");
            }
        }

        // clean up
        ImGui::PopID();
    }

    void Config::clear_light(Light *light, const int alt_index) {
        light->setDeviceIdentifier("");
        light->setIndex(0xFF);
        light->setTemporary(false);
        ::Config::getInstance().updateBinding(
                games_list[games_selected], *light,
                alt_index - 1);
    }
    
    void Config::edit_light_popup(Light &primary_light, Light *light, const int alt_index) {
        if (ImGui::BeginPopupModal("Light Binding", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

            // device selector
            bool control_changed = false;
            if (ImGui::Combo("Device",
                            &this->lights_devices_selected,
                            [] (void* data, int i, const char **item) {
                                *item = ((std::vector<rawinput::Device*>*) data)->at(i)->desc.c_str();
                                return true;
                            },
                            &this->lights_devices, (int) this->lights_devices.size())) {
                this->lights_devices_control_selected = 0;
                control_changed = true;
            }

            // obtain controls
            std::vector<std::string> control_names;
            if (lights_devices_selected >= 0 && lights_devices_selected < (int) lights_devices.size()) {
                auto device = lights_devices[lights_devices_selected];
                switch (device->type) {
                    case rawinput::HID: {
                        size_t index = 0;

                        // add button names
                        for (auto &button_name : device->hidInfo->button_output_caps_names) {

                            // build name
                            std::string name = button_name;
                            if (index > 0xFF)
                                name += " (0x" + bin2hex(&((char*) &index)[1], 1) + bin2hex(&((char*) &index)[0], 1) + ")";
                            else
                                name += " (0x" + bin2hex(&((char*) &index)[0], 1) + ")";

                            // add name
                            control_names.push_back(name);
                            index++;
                        }

                        // add value names
                        for (auto &value_name : device->hidInfo->value_output_caps_names) {

                            // build name
                            std::string name = value_name;
                            if (index > 0xFF)
                                name += " (0x" + bin2hex(&((char*) &index)[1], 1)
                                        + bin2hex(&((char*) &index)[0], 1)
                                        + ", value cap)";
                            else
                                name += " (0x" + bin2hex(&((char*) &index)[0], 1) + ", value cap)";

                            // add name
                            control_names.push_back(name);
                            index++;
                        }

                        break;
                    }
                    case rawinput::SEXTET_OUTPUT: {

                        // add all names of sextet device
                        for (int i = 0; i < rawinput::SextetDevice::LIGHT_COUNT; i++) {
                            std::string name(rawinput::SextetDevice::LIGHT_NAMES[i]);

                            // add name
                            control_names.push_back(name);
                        }
                        break;
                    }
                    case rawinput::PIUIO_DEVICE: {

                        // add all names of PIUIO device
                        for (int i = 0; i < rawinput::PIUIO::PIUIO_MAX_NUM_OF_LIGHTS; i++) {
                            std::string name(rawinput::PIUIO::LIGHT_NAMES[i]);

                            // add name
                            control_names.push_back(name);
                        }
                        break;
                    }
                    case rawinput::SMX_STAGE: {

                        // add all names of SMX device
                        for (int i = 0; i < rawinput::SmxStageDevice::TOTAL_LIGHT_COUNT; i++) {
                            control_names.push_back(rawinput::SmxStageDevice::GetLightNameByIndex(i));
                        }
                        break;
                    }
                    case rawinput::SMX_DEDICAB: {

                        // add all names of SMX dedicab device
                        for (int i = 0; i < rawinput::SmxDedicabDevice::LIGHTS_COUNT; i++) {
                            control_names.push_back(rawinput::SmxDedicabDevice::GetLightNameByIndex(i));
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            // controls
            if (ImGui::Combo("Light Control",
                        &this->lights_devices_control_selected,
                        [] (void* data, int i, const char **item) {
                            *item = ((std::vector<std::string> *) data)->at(i).c_str();
                            return true;
                        },
                        &control_names, control_names.size())) {
                control_changed = true;
            }

            // update light
            if (lights_devices_selected >= 0 && lights_devices_selected < (int) lights_devices.size()) {
                auto identifier = this->lights_devices[lights_devices_selected]->name;
                if (identifier != light->getDeviceIdentifier()) {
                    light->setDeviceIdentifier(identifier);
                    ::Config::getInstance().updateBinding(
                            games_list[games_selected], *light,
                            alt_index - 1);
                }
                if (this->lights_devices_control_selected >= 0) {
                    if ((int) light->getIndex() != this->lights_devices_control_selected) {
                        light->setIndex(this->lights_devices_control_selected);
                        ::Config::getInstance().updateBinding(
                                games_list[games_selected], *light,
                                alt_index - 1);
                    }
                }
            }

            // value preview (use primary light for this)
            ImGui::Separator();
            float value_orig = GameAPI::Lights::readLight(RI_MGR, primary_light);
            float value = value_orig;
            ImGui::SliderFloat("Preview", &value, 0.f, 1.f);

            // manual button controls (use primary light for this as well)
            if (ImGui::Button("Turn On")) {
                value = 1.f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Turn Off")) {
                value = 0.f;
            }

            // manual lock
            if (!cfg::CONFIGURATOR_STANDALONE) {
                ImGui::SameLine();
                ImGui::Checkbox("Lock", &primary_light.override_enabled);
            }

            // apply new value
            if (value != value_orig || control_changed) {
                if (primary_light.override_enabled) {
                    primary_light.override_state = value;
                }
                GameAPI::Lights::writeLight(RI_MGR, primary_light, value);
                RI_MGR->devices_flush_output();
            }

            // close button
            ImGui::Separator();
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
                primary_light.override_enabled = false;
                for (auto &alt : primary_light.getAlternatives()) {
                    alt.override_enabled = false;
                }
            }

            // clean up
            ImGui::EndPopup();
        }
    }

    void Config::auto_match_lights_popup(std::vector<Light> *lights) {
        ImGui::SetNextWindowSize(ImVec2(480.f, 400.f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Auto Match Lights", nullptr, 0)) {
            return;
        }

        // device selector
        ImGui::Text("Select device:");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        bool device_changed = ImGui::Combo("##AutoMatchDevice",
                &this->auto_match_device_selected,
                [] (void* data, int i, const char **item) {
                    auto *devs = (std::vector<rawinput::Device*>*) data;
                    *item = devs->at(i)->desc.c_str();
                    return true;
                },
                &this->auto_match_devices,
                (int) this->auto_match_devices.size());

        rawinput::Device *device = nullptr;
        bool has_device = auto_match_device_selected >= 0
            && auto_match_device_selected < (int) auto_match_devices.size();
        if (has_device) {
            device = auto_match_devices[auto_match_device_selected];
        }

        std::vector<MatchEntry> matched;
        for (int li = 0; li < (int) lights->size(); li++) {
            matched.push_back({
                (*lights)[li].getName(), "", li, -1, false
            });
        }

        std::vector<MatchEntry> unmatched;
        std::vector<std::string> raw_names;
        std::string detected_controller;
        if (has_device) {
            detected_controller = match_lights(device, lights, raw_names, matched, unmatched);
        }

        // partition: matches first, then hard before soft
        std::stable_partition(matched.begin(), matched.end(),
            [](const MatchEntry &e) {
                return e.control_index >= 0;
            });
        matched.erase(
            std::remove_if(matched.begin(), matched.end(),
                [](const MatchEntry &e) {
                    return e.control_index < 0;
                }),
            matched.end());
        std::stable_partition(matched.begin(), matched.end(),
            [](const MatchEntry &e) {
                return !e.soft;
            });

        // check for soft matches
        bool has_soft = false;
        for (auto &entry : matched) {
            if (entry.soft) {
                has_soft = true;
                break;
            }
        }

        // testable entries
        std::vector<int> testable;
        for (int i = 0; i < (int) matched.size(); i++) {
            if (matched[i].control_index >= 0 && (!matched[i].soft || auto_match_soft_enabled)) {
                testable.push_back(i);
            }
        }
        int match_count = (int) testable.size();

        auto write_test_light = [&](int test_idx, float value) {
            if (!device || test_idx < 0 || test_idx >= (int) testable.size()) {
                return;
            }
            auto &entry = matched[testable[test_idx]];
            Light temp(entry.game_name);
            temp.setDeviceIdentifier(device->name);
            temp.setIndex(entry.control_index);
            GameAPI::Lights::writeLight(RI_MGR, temp, value);
            RI_MGR->devices_flush_output();
            if (value > 0.f) {
                auto_match_test_device = device->name;
                auto_match_test_control = entry.control_index;
            } else {
                auto_match_test_device.clear();
            }
        };

        auto stop_test = [&]() {
            if (!auto_match_testing) {
                return;
            }
            write_test_light(auto_match_test_current, 0.f);
            auto_match_testing = false;
            auto_match_test_current = -1;
        };

        if (device_changed) {
            stop_test();
            auto_match_copied = false;

            // auto-detect P1/P2 from existing button bindings
            auto_match_p2 = false;
            if (device) {
                auto *buttons = games::get_buttons(this->games_selected_name);
                if (buttons) {
                    int p1_count = 0, p2_count = 0;
                    for (auto &btn : *buttons) {
                        if (btn.getDeviceIdentifier() != device->name) {
                            continue;
                        }
                        auto &bn = btn.getName();
                        if (bn.substr(0, 3) == "P1 ") {
                            p1_count++;
                        } else if (bn.substr(0, 3) == "P2 ") {
                            p2_count++;
                        }
                    }
                    if (p2_count > p1_count) {
                        auto_match_p2 = true;
                    }
                }
            }
        }

        // test sequence
        if (auto_match_testing && !testable.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - auto_match_test_time).count();
            if (elapsed >= 500) {
                write_test_light(auto_match_test_current, 0.f);
                auto_match_test_current++;
                if (auto_match_test_current >= (int) testable.size()) {
                    auto_match_test_current = 0;
                }
                auto_match_test_time = now;
                write_test_light(auto_match_test_current, 1.f);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!detected_controller.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.9f, 0.3f, 1.f),
                "%s detected", detected_controller.c_str());
            if (has_soft) {
                float help_w = ImGui::CalcTextSize("(?)").x;
                float text_w = ImGui::CalcTextSize("Include suggested matches").x;
                float cb_w = ImGui::GetFrameHeight();
                float spacing = ImGui::GetStyle().ItemSpacing.x;
                float total = help_w + spacing + text_w + spacing + cb_w;
                ImGui::SameLine(ImGui::GetWindowWidth() - total
                    - ImGui::GetStyle().WindowPadding.x);
                ImGui::HelpMarker(
                    "Include suggested device/game light matches\n"
                    "(based on common/known controller lights)");
                ImGui::SameLine();
                ImGui::TextUnformatted("Include suggested matches");
                ImGui::SameLine();
                ImGui::Checkbox("##soft_match", &auto_match_soft_enabled);
            }
        }

        if (has_device) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Matched: %d lights", match_count);
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::Dummy(ImVec2(0, ImGui::GetFrameHeight()));
        }

        // P1/P2 toggle + test lights button (right-aligned)
        {
            bool has_players = false;
            for (auto &light : *lights) {
                auto &ln = light.getName();
                if (ln.substr(0, 3) == "P1 " || ln.substr(0, 3) == "P2 ") {
                    has_players = true;
                    break;
                }
            }

            const char *test_label = auto_match_testing ? "Stop Testing" : "Test Lights";
            float test_w = ImGui::CalcTextSize(test_label).x
                + ImGui::GetStyle().FramePadding.x * 2;
            float total_w = test_w;

            const char *p_label = auto_match_p2 ? "P2" : "P1";
            float p_w = 0;
            if (has_players) {
                p_w = ImGui::CalcTextSize(p_label).x
                    + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
                total_w += p_w;
            }

            ImGui::SameLine(ImGui::GetWindowWidth() - total_w
                - ImGui::GetStyle().WindowPadding.x);

            if (has_players) {
                if (ImGui::Button(p_label)) {
                    stop_test();
                    auto_match_p2 = !auto_match_p2;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Toggle between P1 and P2 light mappings.\n"
                        "Auto-detected from button bindings.");
                }
                ImGui::SameLine();
            }

            ImGui::BeginDisabled(!auto_match_testing && match_count == 0);
            if (ImGui::Button(test_label)) {
                if (auto_match_testing) {
                    stop_test();
                } else {
                    auto_match_testing = true;
                    auto_match_test_current = 0;
                    auto_match_test_time = std::chrono::steady_clock::now();
                    write_test_light(0, 1.f);
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                ImGui::SetTooltip("Cycle through matched lights sequentially.");
            }
        }

        // match table
        ImGui::Spacing();
        float footer_h = ImGui::GetStyle().ItemSpacing.y
            + ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().WindowPadding.y;
        float table_h = ImGui::GetContentRegionAvail().y - footer_h;
        if (ImGui::BeginTable("##AutoMatchTable", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                ImVec2(0, table_h))) {
            ImGui::TableSetupColumn("Device Light", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.f);
            ImGui::TableSetupColumn("Game Light", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int) matched.size(); i++) {
                auto &entry = matched[i];
                bool is_active_test = auto_match_testing
                    && auto_match_test_current >= 0
                    && auto_match_test_current < (int) testable.size()
                    && testable[auto_match_test_current] == i;
                bool soft_inactive = entry.soft && !auto_match_soft_enabled;

                ImGui::TableNextRow();

                if (is_active_test) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.2f, 0.2f, 1.f));
                } else if (soft_inactive) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
                } else if (entry.soft) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.9f, 0.3f, 1.f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.f, 0.5f, 1.f));
                }

                // device light
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.device_name.c_str());

                // arrow (centered)
                ImGui::TableNextColumn();
                float arrow_w = ImGui::CalcTextSize("->").x;
                float col_w = ImGui::GetColumnWidth();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col_w - arrow_w) * 0.5f);
                ImGui::TextUnformatted("->");

                // game light
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.game_name.c_str());

                ImGui::PopStyleColor();
            }

            for (auto &entry : unmatched) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", entry.device_name.c_str());
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
            }

            ImGui::EndTable();
        }

        // apply / cancel
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginDisabled(match_count == 0);
        if (ImGui::Button("Save")) {
            stop_test();

            // look up current device desc for same-controller detection
            auto get_device_desc = [&](const std::string &identifier) -> std::string {
                for (auto &d : RI_MGR->devices_get()) {
                    if (d.name == identifier) {
                        return d.desc;
                    }
                }
                return "";
            };

            for (auto &entry : matched) {
                if (entry.control_index < 0) {
                    continue;
                }
                if (entry.soft && !auto_match_soft_enabled) {
                    continue;
                }
                auto &light = (*lights)[entry.light_index];

                // if primary is already bound to a different unit of the same
                // controller type, add as alternative instead of overwriting
                bool use_alt = false;
                if (!light.getDeviceIdentifier().empty()
                        && light.getDeviceIdentifier() != device->name
                        && get_device_desc(light.getDeviceIdentifier()) == device->desc) {
                    use_alt = true;
                }

                if (use_alt) {
                    // find or create an alternative slot
                    Light *alt = nullptr;
                    for (auto &a : light.getAlternatives()) {
                        if (!a.isValid() || a.getDeviceIdentifier() == device->name) {
                            alt = &a;
                            break;
                        }
                    }
                    if (!alt && light.getAlternatives().size() < 7) {
                        Light temp(light.getName());
                        temp.setTemporary(true);
                        light.getAlternatives().push_back(temp);
                        alt = &light.getAlternatives().back();
                    }
                    if (alt) {
                        alt->setDeviceIdentifier(device->name);
                        alt->setIndex(entry.control_index);
                        int alt_idx = (int)(&*alt - &light.getAlternatives()[0]);
                        ::Config::getInstance().updateBinding(
                            games_list[games_selected], *alt, alt_idx);
                    }
                } else {
                    light.setDeviceIdentifier(device->name);
                    light.setIndex(entry.control_index);
                    ::Config::getInstance().updateBinding(
                        games_list[games_selected], light, -1);
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
            ImGui::SetTooltip("Save and apply matched light outputs.");
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            stop_test();
            ImGui::CloseCurrentPopup();
        }

        // copy list (right-justified)
        if (auto_match_copied) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - auto_match_copy_time).count();
            if (elapsed >= 2000) {
                auto_match_copied = false;
            }
        }
        {
            const char *copy_label = auto_match_copied ? "Copied" : "Copy List";
            float copy_w = ImGui::CalcTextSize(copy_label).x
                + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SameLine(ImGui::GetWindowWidth() - copy_w
                - ImGui::GetStyle().WindowPadding.x);
            ImGui::BeginDisabled(raw_names.empty() || auto_match_copied);
            if (ImGui::Button(copy_label)) {
                std::string list;
                for (int i = 0; i < (int) raw_names.size(); i++) {
                    if (!list.empty()) {
                        list += "\n";
                    }
                    char addr[16];
                    if (i > 0xFF) {
                        snprintf(addr, sizeof(addr), "0x%04X", i);
                    } else {
                        snprintf(addr, sizeof(addr), "0x%02X", i);
                    }
                    list += raw_names[i] + " (" + addr + ")";
                }
                clipboard::copy_text(list);
                auto_match_copied = true;
                auto_match_copy_time = std::chrono::steady_clock::now();
            }
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
            ImGui::SetTooltip("Copy full list of device lights (for development).");
        }

        ImGui::EndPopup();
    }

    std::string Config::match_lights(
            rawinput::Device *device,
            std::vector<Light> *lights,
            std::vector<std::string> &raw_names,
            std::vector<MatchEntry> &matched,
            std::vector<MatchEntry> &unmatched) {

        std::string detected_controller;

        // build output name list
        switch (device->type) {
            case rawinput::HID: {
                for (auto &n : device->hidInfo->button_output_caps_names) {
                    raw_names.push_back(n);
                }
                for (auto &n : device->hidInfo->value_output_caps_names) {
                    raw_names.push_back(n);
                }
                break;
            }
            case rawinput::SEXTET_OUTPUT: {
                for (int i = 0; i < rawinput::SextetDevice::LIGHT_COUNT; i++) {
                    raw_names.emplace_back(
                        rawinput::SextetDevice::LIGHT_NAMES[i]);
                }
                break;
            }
            case rawinput::PIUIO_DEVICE: {
                for (int i = 0; i < rawinput::PIUIO::PIUIO_MAX_NUM_OF_LIGHTS; i++) {
                    raw_names.emplace_back(
                        rawinput::PIUIO::LIGHT_NAMES[i]);
                }
                break;
            }
            case rawinput::SMX_STAGE: {
                for (int i = 0; i < rawinput::SmxStageDevice::TOTAL_LIGHT_COUNT; i++) {
                    raw_names.push_back(
                        rawinput::SmxStageDevice::GetLightNameByIndex(i));
                }
                break;
            }
            case rawinput::SMX_DEDICAB: {
                for (int i = 0; i < rawinput::SmxDedicabDevice::LIGHTS_COUNT; i++) {
                    raw_names.push_back(
                        rawinput::SmxDedicabDevice::GetLightNameByIndex(i));
                }
                break;
            }
            default:
                break;
        }

        // match by name (hard match, case insensitive, with P1/P2 prefix fallback)
        std::vector<bool> device_matched(raw_names.size(), false);
        const std::string player_prefix = auto_match_p2 ? "p2 " : "p1 ";

        for (auto &entry : matched) {
            const auto game_lower = strtolower(entry.game_name);

            // match on exact name
            for (int ci = 0; ci < (int) raw_names.size(); ci++) {
                if (device_matched[ci]) {
                    continue;
                }

                const auto dev_lower = strtolower(raw_names[ci]);
                if (game_lower == dev_lower ||
                    game_lower == player_prefix + dev_lower) {

                    entry.device_name = raw_names[ci];
                    entry.control_index = ci;
                    device_matched[ci] = true;
                    break;
                }
            }

            // raw device name, but stripped of a few common strings, and spaces
            for (int ci = 0; ci < (int) raw_names.size(); ci++) {
                if (device_matched[ci]) {
                    continue;
                }

                auto dev_stripped = strtolower(raw_names[ci]);;
                strreplace(dev_stripped, "button", "");
                strreplace(dev_stripped, "led", "");
                strreplace(dev_stripped, "light", "");
                strreplace(dev_stripped, " ", "");

                if (game_lower == dev_stripped ||
                    game_lower == player_prefix + dev_stripped) {

                    entry.device_name = raw_names[ci];
                    entry.control_index = ci;
                    device_matched[ci] = true;
                    break;
                }
            }
        }

        // soft matching
        const bool is_valkyrie_mode = games::sdvx::is_valkyrie_model();
        static const char *RGB[] = {"R", "G", "B"};

        // try to match a game light name, first as-is, then with P1/P2 prefix
        auto try_match = [&](const std::string &game_target, int ci, const char *controller) -> bool {
            auto target = strtolower(game_target);
            auto target_player = strtolower(player_prefix + game_target);
            for (auto &entry : matched) {
                if (entry.control_index >= 0) {
                    continue;
                }
                auto entry_lower = strtolower(entry.game_name);
                if (entry_lower != target && entry_lower != target_player) {
                    continue;
                }
                entry.device_name = raw_names[ci];
                entry.control_index = ci;
                entry.soft = true;
                device_matched[ci] = true;
                if (detected_controller.empty()) {
                    detected_controller = controller;
                }
                return true;
            }
            return false;
        };

        for (int ri = 0; ri < LIGHT_MATCH_MAP_COUNT; ri++) {
            auto &rule = LIGHT_MATCH_MAP[ri];
            if (*rule.game && this->games_selected_name != rule.game) {
                continue;
            }
            if (rule.vid && device->hidInfo
                    && (device->hidInfo->attributes.VendorID != rule.vid
                        || device->hidInfo->attributes.ProductID != rule.pid)) {
                continue;
            }

            if (*rule.address) {
                // address mode: match device light at specific index
                int ci = (int) strtol(rule.address, nullptr, 0);
                if (ci >= (int) raw_names.size() || device_matched[ci]) {
                    continue;
                }
                if (strtolower(raw_names[ci]) != strtolower(std::string(rule.device_light))) {
                    continue;
                }
                try_match(rule.game_light, ci, rule.controller);
            } else if (rule.rgb) {
                // RGB mode: expand device_light + " R/G/B" + device_suffix
                const char *game_base = (is_valkyrie_mode && *rule.game_light_alt)
                    ? rule.game_light_alt : rule.game_light;
                if (!*game_base) {
                    continue;
                }
                for (int ci = 0; ci < (int) raw_names.size(); ci++) {
                    if (device_matched[ci]) {
                        continue;
                    }
                    auto dev_lower = strtolower(raw_names[ci]);
                    for (int rgb = 0; rgb < 3; rgb++) {
                        // try light name, plus space, plus R/G/B
                        auto expected = strtolower(
                            std::string(rule.device_light) + " " + RGB[rgb] + rule.device_suffix);
                        if (dev_lower == expected) {
                            try_match(std::string(game_base) + " " + RGB[rgb], ci, rule.controller);
                            break;
                        }
                        // try light name, plus R/G/B
                        auto expected2 = strtolower(
                            std::string(rule.device_light) + RGB[rgb] + rule.device_suffix);
                        if (dev_lower == expected2) {
                            try_match(std::string(game_base) + " " + RGB[rgb], ci, rule.controller);
                            break;
                        }
                    }
                }
            } else {
                // name mode: direct 1:1 device_light → game_light
                const char *game_base = (is_valkyrie_mode && *rule.game_light_alt)
                    ? rule.game_light_alt : rule.game_light;
                if (!*game_base) {
                    continue;
                }
                for (int ci = 0; ci < (int) raw_names.size(); ci++) {
                    if (device_matched[ci]) {
                        continue;
                    }
                    if (strtolower(raw_names[ci]) != strtolower(std::string(rule.device_light))) {
                        continue;
                    }
                    if (try_match(game_base, ci, rule.controller)) {
                        break;
                    }
                }
            }
        }

        // unmatched device lights
        for (int ci = 0; ci < (int) raw_names.size(); ci++) {
            if (!device_matched[ci] && strtolower(raw_names[ci]) != "null") {
                unmatched.push_back({"", raw_names[ci], -1, ci, false});
            }
        }

        return detected_controller;
    }
    
    void Config::build_cards() {

        constexpr float TEXT_INPUT_WIDTH = 240.f;

        // early quit
        if (this->games_selected < 0 || this->games_selected_name.empty()) {
            ImGui::Text("Please select a game first.");
            return;
        }

        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Card overrides");
        ImGui::Spacing();
        ImGui::TextUnformatted(
            "Specify hardcoded card numbers here. This will always take priority when Insert Card is pressed.");
        ImGui::Spacing();

        // read in values from options
        auto options = games::get_options(this->games_selected_name);
        for (int player = 0; player < 2; player++) {
            ImGui::PushID(("OverrideCardP" + to_string(player)).c_str());

            // intentionally using the same buffer length as options tab
            char buffer[OPTION_INPUT_TEXT_WIDTH];
            bool card_changed = false;

            // read in values from options
            auto &option = (player == 0)
                    ? options->at(launcher::Options::Player1Card)
                    : options->at(launcher::Options::Player2Card);
            if (option.is_active()) {
                strncpy(buffer, option.value.c_str(), sizeof(buffer) - 1);
            } else {
                buffer[0] = '\0';
            }

            const bool tree_open  = ImGui::TreeNodeEx("##OverrideCardTree", ImGuiTreeNodeFlags_DefaultOpen);
            if (this->keypads_card_override_valid[player]) {
                ImGui::PushStyleColor(ImGuiCol_Text, TEXT_COLOR_GREEN);
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Text("Player %i (-card%i)", player + 1, player);
            if (this->keypads_card_override_valid[player]) {
                ImGui::PopStyleColor();
            }

            if (tree_open) {
                // card number box
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    this->keypads_card_override_valid[player] ? ImVec4(1.f, 1.f, 1.f, 1.f) :
                    ImVec4(1.f, 0.f, 0.f, 1.f));
                ImGui::SetNextItemWidth(TEXT_INPUT_WIDTH);
                ImGui::BeginDisabled(option.disabled);
                ImGui::InputTextWithHint("##OverrideCard", "E004010000000000",
                        buffer, sizeof(buffer) - 1,
                        ImGuiInputTextFlags_CharsUppercase |
                        ImGuiInputTextFlags_CharsHexadecimal |
                        ImGuiInputTextFlags_EscapeClearsAll);
                ImGui::PopStyleColor();
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    card_changed = true;
                }

                // check if valid on every frame;
                // do this after the widget and remember the result for next frame
                this->keypads_card_override_valid[player] = false;
                const auto buffer_len = strlen(buffer);
                if (buffer_len == 16) {
                    this->keypads_card_override_valid[player] = validate_ea_card(buffer);
                } else if (buffer_len == 0) {
                    this->keypads_card_override_valid[player] = false;
                }

                ImGui::EndDisabled();

                if (!option.disabled) {
                    // generate button
                    ImGui::SameLine();
                    if (ImGui::Button("Generate")) {
                        generate_ea_card(buffer);
                        card_changed = true;
                    }

                    // clear button
                    if (option.is_active()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            buffer[0] = '\0';
                            card_changed = true;
                        }
                    }
                } else {
                    if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                        ImGui::HelpTooltip(
                            "This option cannot be edited because it was overriden by command-line options.\n"
                            "Run spicecfg.exe to configure the options and then run spice(64).exe directly.");
                    }
                }

                // bad card number warning
                if (!this->keypads_card_override_valid[player] && buffer_len > 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Invalid card number");
                }

                ImGui::TreePop();
            }

            if (player == 0) {
                ImGui::Spacing();
                ImGui::Spacing();
            }

            ImGui::PopID();

            if (card_changed) {
                this->options_dirty = true;
                option.value = buffer;
                ::Config::getInstance().updateBinding(games_list[games_selected], option);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Card from text files");
        ImGui::Spacing();
        ImGui::TextUnformatted(
            "Use text files on disk; its content will be read when Insert Card is pressed.");
        ImGui::Spacing();

        // get bindings and copy paths
        auto game = games_list[games_selected];
        auto bindings = ::Config::getInstance().getKeypadBindings(this->games_selected_name);
        bool bindings_updated = false;
        for (int player = 0; player < 2; player++) {
            strncpy(this->keypads_card_path[player],
                    bindings.card_paths[player].string().c_str(),
                    sizeof(this->keypads_card_path[0]) - 1);
            this->keypads_card_path[player][sizeof(this->keypads_card_path[0]) - 1] = '\0';
        }

        // card settings for each player
        for (int player = 0; player < 2; player++) {

            // check overrides first
            const auto &override = (player == 0)
                ? options->at(launcher::Options::Player1Card)
                : options->at(launcher::Options::Player2Card);

            // custom ID and title
            ImGui::PushID(("KeypadP" + to_string(player)).c_str());
            const bool tree_open = ImGui::TreeNodeEx("##KeypadTreeNode", ImGuiTreeNodeFlags_DefaultOpen);
            const bool card_content_valid =
                !override.is_active() &&
                this->keypads_card_file_contents_valid[player] &&
                this->keypads_card_number[player][0] != 0;
            if (card_content_valid) {
                ImGui::PushStyleColor(ImGuiCol_Text, TEXT_COLOR_GREEN);
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Text("Player %i (card%i.txt)", player + 1, player);
            if (card_content_valid) {
                ImGui::PopStyleColor();
            }

            if (tree_open) {
                if (override.is_active()) {
                    ImGui::TextDisabled("Disabled - override is in use.");
                } else {
                    // card path
                    std::string hint = "using .\\card" + to_string(player) + ".txt (default)";
                    ImGui::SetNextItemWidth(TEXT_INPUT_WIDTH);
                    if (ImGui::InputTextWithHint("File Path", hint.c_str(),
                            this->keypads_card_path[player], sizeof(this->keypads_card_path[0]) - 1))
                    {
                        bindings.card_paths[player] = this->keypads_card_path[player];
                        bindings_updated = true;
                    }

                    // help marker
                    ImGui::SameLine();
                    ImGui::HelpMarker("Leave this empty to use the card file in your game directory.\n"
                                    "Hint: You can place 'card0.txt' (P1) / 'card1.txt' (P2) into the root of your USB "
                                    "flash drive and it will trigger a card insert when you connect it!");

                    // card path file selector
                    ImGui::SameLine();
                    if (ImGui::Button("Open...")) {

                        // standalone version opens native file browser
                        if (cfg::CONFIGURATOR_STANDALONE && !keypads_card_select) {

                            // run in separate thread otherwise we get a crash
                            keypads_card_select_done = false;
                            keypads_card_select = new std::thread([this, bindings, player, game] {

                                // open dialog to get path
                                auto ofn_path = std::make_unique<wchar_t[]>(512);
                                ofn_path[0] = L'\0';
                                OPENFILENAMEW ofn {};
                                ofn.lStructSize = sizeof(ofn);
                                ofn.lpstrFile = ofn_path.get();
                                ofn.nMaxFile = 512;
                                ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR;
                                ofn.lpstrDefExt = L"txt";
                                ofn.lpstrInitialDir = L".";

                                // check for success
                                auto guard = rawinput::set_os_window_focus_guard();
                                if (GetSaveFileNameW(&ofn)) {

                                    // update card path
                                    auto new_bindings = bindings;
                                    new_bindings.card_paths[player] = std::filesystem::path(ofn_path.get());
                                    ::Config::getInstance().updateBinding(game, new_bindings);
                                    eamuse_update_keypad_bindings();

                                    read_card(player);
                                } else {
                                    auto error = CommDlgExtendedError();
                                    if (error) {
                                        log_warning("cfg", "failed to get save file name: {}", error);
                                    } else {
                                        log_warning("cfg", "failed to get save file name");
                                    }
                                }

                                // clean up
                                keypads_card_select_done = true;
                            });
                        }

                        // in-game version opens ImGui file browser
                        if (!cfg::CONFIGURATOR_STANDALONE && !this->keypads_card_select_browser[player].IsOpened()) {
                            this->keypads_card_select_browser[player].SetTitle("Card Select");
                            this->keypads_card_select_browser[player].SetTypeFilters({".txt", "*"});
                            // this->keypads_card_select_browser[player].flags_ |= ImGuiFileBrowserFlags_EnterNewFilename;
                            this->keypads_card_select_browser[player].Open();
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Edit")) {
                        std::filesystem::path path;
                        if (!bindings.card_paths[player].empty()) {
                            path = bindings.card_paths[player];
                        } else {
                            path = (player > 0) ? "card1.txt" : "card0.txt";
                        }
                        // using notepad here in case the file doesn't exist
                        launch_shell("notepad.exe", path.string().c_str());
                    }

                    // clear button
                    if (!bindings.card_paths[player].empty()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            bindings.card_paths[player] = "";
                            bindings_updated = true;
                        }
                    }

                    // verify card number
                    this->keypads_card_file_contents_valid[player] = true;
                    if (this->keypads_card_number[player][0] != 0) {
                        this->keypads_card_file_contents_valid[player] = 
                            validate_ea_card(this->keypads_card_number[player]);
                    }

                    // card number box
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        this->keypads_card_file_contents_valid[player] ? ImVec4(1.f, 1.f, 1.f, 1.f) :
                        ImVec4(1.f, 0.f, 0.f, 1.f));
                    ImGui::SetNextItemWidth(TEXT_INPUT_WIDTH);
                    ImGui::BeginDisabled();
                    if (this->keypads_card_number[player][0] != 0) {
                        ImGui::Text("Card from file: %s", this->keypads_card_number[player]);
                    } else {
                        ImGui::TextUnformatted("Failed to read file.");
                    }
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor();
                }

                ImGui::TreePop();
            }

            // render card select browser
            this->keypads_card_select_browser[player].Display();
            if (this->keypads_card_select_browser[player].HasSelected()) {
                auto selected = keypads_card_select_browser[player].GetSelected();
                this->keypads_card_select_browser[player].ClearSelected();
                bindings.card_paths[player] = selected;
                bindings_updated = true;
            }

            // clean up thread when needed
            if (keypads_card_select_done) {
                keypads_card_select_done = false;
                keypads_card_select->join();
                delete keypads_card_select;
                keypads_card_select = nullptr;
            }

            // clean up
            ImGui::PopID();

            if (player == 0) {
                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        // check for binding update and save
        if (bindings_updated) {
            ::Config::getInstance().updateBinding(game, bindings);
            eamuse_update_keypad_bindings();
            read_card();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "NFC card reader status");
        ImGui::Spacing();
        if (cfg::CONFIGURATOR_STANDALONE) {

            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("Test NFC card readers and card insertions over API.");
            ImGui::SameLine();
            ImGui::HelpMarker(
                "Enable card readers in Advanced tab, under Card Readers section, and restart. "
                "Alternatively, launch spicecfg in command line and pass in the correct parameters. "
                "BT5API readers currently do not show the card number in this UI.");
            ImGui::Spacing();

            // show scanned card numbers
            for (int player = 0; player < 2; player++) {
                ImGui::PushID(("CardReaderDisp" + to_string(player)).c_str());
                if (ImGui::TreeNodeEx(
                        fmt::format("Last card detected for player {}", player + 1).c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen)) {

                    char card_uid[8];
                    const bool card_present = eamuse_scanned_card_peek_noninvasive(player, card_uid);
                    if (card_present) {
                        ImGui::AlignTextToFramePadding();
                        const auto card_str = bin2hex(card_uid, 8);
                        ImGui::Text(
                            "%s %s %s %s",
                            card_str.substr(0, 4).c_str(),
                            card_str.substr(4, 4).c_str(),
                            card_str.substr(8, 4).c_str(),
                            card_str.substr(12, 4).c_str()
                            );
                        ImGui::SameLine();
                        if (ImGui::Button("Copy")) {
                            clipboard::copy_text(card_str);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            eamuse_scanned_card_clear(player);
                        }
                    } else {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Card not present");
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();
                if (player == 0) {
                    ImGui::Spacing();
                    ImGui::Spacing();
                }
            }

        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", "Only available in spicecfg.");
            ImGui::Spacing();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "More tips");
        ImGui::Spacing();
        ImGui::BeginDisabled();
        ImGui::TextWrapped("To debug card reader issues, run spice.exe -cfg in command line and check the log.");
        ImGui::TextWrapped(
            "If you have multiple players, try opening Card Manager window in the game. "
            "Check the key bind in Overlay tab for Toggle Card Manager.");
        ImGui::EndDisabled();
    }

    bool Config::validate_ea_card(char card_number[16]) {
        for (int n = 0; n < 16; n++) {
            char c = card_number[n];
            bool digit = c >= '0' && c <= '9';
            bool character_big = c >= 'A' && c <= 'F';
            bool character_small = c >= 'a' && c <= 'f';
            if (!digit && !character_big && !character_small) {
                return false;
            }
        }
        return true;
    }

    std::string Config::build_option_value_picker_title(const OptionDefinition& definition) {
        // need to make these all unique since they are also used as ID
        // if not unique, append ## per ImGui rules to create unique ones
        switch (definition.picker) {
            case OptionPickerType::AsioDriver:
                return "ASIO Driver Picker";
            case OptionPickerType::EACard:
                return "EA Card Picker";
            case OptionPickerType::CpuAffinity:
                return "CPU Affinity Picker";
            case OptionPickerType::FilePath:
                return "File Picker";
            case OptionPickerType::DirectoryPath:
                return "Folder Picker";
            default:
                return "Unknown Picker";
        };
    }

    void Config::build_option_value_picker(Option& option) {
        auto &definition = option.get_definition();
        if (definition.picker == OptionPickerType::AsioDriver) {
            if (asio_driver_list == nullptr) {
                asio_driver_list = std::make_unique<AsioDriverList>();
            }

            ImGui::TextUnformatted("If your ASIO driver is not shown here, close this");
            ImGui::TextUnformatted("popup and enter the driver name manually.");
            ImGui::SameLine();
            ImGui::HelpMarker(
                "This list is populated by scanning the registry for ASIO drivers.\n\n"
                "If your driver is not showing up, it may be because it is not properly registered in the system.\n\n"
                "For 64-bit games, check in HKLM\\SOFTWARE\\ASIO\\.\n\n"
                "For 32-bit games on 64-bit Windows, check in HKLM\\SOFTWARE\\WOW6432Node\\ASIO\\.\n\n"
                "spicecfg runs in 32-bit, so it may not see 64-bit-only drivers.");

            ImGui::TextUnformatted("");
            if (asio_driver_list->driver_list.empty()) {
                ImGui::TextUnformatted("No ASIO drivers found.");
            } else {
                ImGui::TextUnformatted("Pick from ASIO drivers:");
                ImGui::SetNextItemWidth(300.f);
                if (ImGui::BeginListBox("##asiodrivers")) {
                    for (const auto &driver : asio_driver_list->driver_list) {
                        const bool is_selected = option.value == std::string(driver.name);
                        if (ImGui::Selectable(fmt::format("[{}] {}", driver.id, driver.name).c_str(), is_selected)) {
                            option.value = driver.name;
                        }
                    }
                    ImGui::EndListBox();
                }
            }
        } else if (definition.picker == OptionPickerType::EACard) {
            ImGui::TextUnformatted("Generate a new card number:");
            if (ImGui::Button("Generate")) {
                char new_card[17];
                generate_ea_card(new_card);
                option.value = new_card;
            }
        } else if (definition.picker == OptionPickerType::CpuAffinity) {
            ImGui::TextUnformatted("Requires restart! Showing all procs in Group 0.");
            ImGui::TextUnformatted("");
            ImGui::TextUnformatted("Pick CPU cores to use:");
            const uint64_t cpu_count = GetActiveProcessorCount(0);
            uint64_t affinity = 0;
            if (!option.value.empty()) {
                try {
                    affinity = std::stoull(option.value, nullptr, 16);
                } catch (const std::exception &ex) {
                    option.value = "";
                    affinity = 0;
                }
            }
            ImGui::BeginChild(
                "##cpuaffinity",
                ImVec2(
                    0,
                    ImGui::GetFrameHeightWithSpacing() * std::clamp(cpu_count / 4, 2ull, 4ull))
                );

            bool selection_changed = false;
            uint64_t set_bits = 0;
            for (uint64_t i = 0; i < cpu_count; i++) {
                if (i % 4 != 0) {
                    ImGui::SameLine();
                }

                bool selected = (affinity & (1ULL << i)) != 0;
                if (ImGui::Checkbox(fmt::format("CPU {}", i).c_str(), &selected)) {
                    selection_changed = true;
                    if (selected) {
                        affinity |= (1ULL << i);
                    } else {
                        affinity &= ~(1ULL << i);
                    }
                }
                if (selected) {
                    set_bits++;
                }
            }
            if (selection_changed) {
                if (set_bits == 0) {
                    option.value = "";
                } else {
                    option.value = fmt::format("0x{:X}", affinity);
                }
            }
            
            ImGui::EndChild();
            if (option.value.empty()) {
                ImGui::TextUnformatted("Using all CPUs (option default).");
            } else {
                ImGui::TextUnformatted("Using selected CPUs.");
            }
        } else if (definition.picker == OptionPickerType::FilePath) {
            if (!cfg::CONFIGURATOR_STANDALONE) {
                ImGui::BeginDisabled();
                ImGui::TextUnformatted("File browser only works in spicecfg.");
            }
            if (ImGui::Button("Select File...")) {
                // run in separate thread otherwise we get a crash
                if (!file_picker_thread) {
                    file_picker_done = false;
                    file_picker_path = "";
                    file_picker_thread = new std::thread([this, &definition] {

                        std::wstring extensions;
                        if (!definition.file_extension.empty()) {
                            const std::wstring ext = s2ws(definition.file_extension);
                            // filter to file extension preferred by the option (e.g., DLL)
                            extensions = ext + L" Files (*." + ext + L")";
                            extensions.push_back(L'\0');
                            extensions += L"*." + ext;
                            extensions.push_back(L'\0');
                            // also add "All files" filter
                            extensions += L"All Files (*.*)";
                            extensions.push_back(L'\0');
                            extensions += L"*.*";
                            extensions.push_back(L'\0');
                            // eol
                            extensions.push_back(L'\0');
                        } else {
                            extensions = L"All Files (*.*)";
                            extensions.push_back(L'\0');
                            extensions += L"*.*";
                            extensions.push_back(L'\0');
                            extensions.push_back(L'\0');
                        }

                        // open dialog to get path
                        auto ofn_path = std::make_unique<wchar_t[]>(512);
                        ofn_path[0] = L'\0';
                        OPENFILENAMEW ofn{};
                        ofn.lStructSize = sizeof(ofn);
                        ofn.lpstrFilter = extensions.c_str();
                        ofn.lpstrFile = ofn_path.get();
                        ofn.nMaxFile = 512;
                        ofn.nFilterIndex = 1;
                        ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR;
                        ofn.lpstrInitialDir = L".";

                        // check for success
                        auto guard = rawinput::set_os_window_focus_guard();
                        if (GetSaveFileNameW(&ofn)) {
                            file_picker_path = std::filesystem::path(ofn_path.get());
                        }

                        // clean up
                        file_picker_done = true;
                    });
                }
            }

            if (file_picker_done) {
                file_picker_done = false;
                file_picker_thread->join();
                delete file_picker_thread;
                file_picker_thread = nullptr;
                if (!file_picker_path.empty()) {
                    option.value = file_picker_path.string();
                }
            }

            if (!cfg::CONFIGURATOR_STANDALONE) {
                ImGui::EndDisabled();
            }
        } else if (definition.picker == OptionPickerType::DirectoryPath) {
            if (!cfg::CONFIGURATOR_STANDALONE) {
                ImGui::BeginDisabled();
                ImGui::TextUnformatted("File browser only works in spicecfg.");
            }
            if (ImGui::Button("Browse...")) {
                // run in separate thread otherwise we get a crash
                if (!file_picker_thread) {
                    file_picker_done = false;
                    file_picker_path = "";
                    file_picker_thread = new std::thread([this] {
                        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

                        const auto spice_bin_path =
                            libutils::module_file_name(nullptr).parent_path().wstring();

                        // SHBrowseForFolderW sucks, but the alternatives are:
                        // 1. Use IFileDialog, which requires pulling in more Windows dependencies
                        // 2. use ImGui::FileBrowser
                        // both are acceptable but sticking to legacy UI for now for simplicity
                        BROWSEINFOW info{};
                        info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                        info.lpfn = BrowseCallbackProc;
                        info.lParam = reinterpret_cast<LPARAM>(spice_bin_path.c_str());
                        auto guard = rawinput::set_os_window_focus_guard();
                        auto pidl = SHBrowseForFolderW(&info);
                        if (pidl) {
                            wchar_t path[MAX_PATH];
                            std::filesystem::path result;
                            if (SHGetPathFromIDListW(pidl, path)) {
                                file_picker_path = path;
                            }
                            CoTaskMemFree(pidl);
                        } else {
                            file_picker_path = "";
                        }

                        // clean up
                        file_picker_done = true;
                        CoUninitialize();
                    });
                }
            }

            if (file_picker_done) {
                file_picker_done = false;
                file_picker_thread->join();
                delete file_picker_thread;
                file_picker_thread = nullptr;
                if (!file_picker_path.empty()) {
                    option.value = file_picker_path.string();
                }
            }

            if (!cfg::CONFIGURATOR_STANDALONE) {
                ImGui::EndDisabled();
            }

        } else {
            ImGui::TextUnformatted("No picker available for this option. How did you get here?");
        }
    }

    void Config::build_options(
        std::vector<Option> *options, const std::string &category, const std::string *filter) {
        int options_count;  

        // category name
        std::string cat = "Options";
        if (!category.empty()) {
            cat = category;
        } else if (filter != nullptr) {
            cat = "Search results";
        }
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "%s", cat.c_str());
        ImGui::Separator();

        // render table
        // tables must share the same ID to have synced column settings
        if (ImGui::BeginTable("OptionsTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Option", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(
                "CMD Line Parameter",
                ImGuiTableColumnFlags_WidthFixed,
                overlay::apply_scaling(216));
            ImGui::TableSetupColumn(
                "Setting",
                ImGuiTableColumnFlags_WidthFixed,
                overlay::apply_scaling(240));

            // iterate options
            options_count = 0;
            for (auto &option : *options) {
                
                // get option definition
                auto &definition = option.get_definition();
                
                // check category
                if (!category.empty() && definition.category != category) {
                    continue;
                }
                
                // check hidden option
                if (!this->options_show_hidden && option.value.empty()) {
                    // skip hidden entries
                    if (definition.hidden) {
                        continue;
                    }
                }

                // check for game exclusivity
                if (!definition.game_name.empty()) {
                    if (definition.game_name != this->games_selected_name) {
                        continue;
                    }
                }
                
                // filter
                if (filter != nullptr) {
                    if (filter->empty()) {
                        continue;
                    }
                    if (!option.search_match(*filter)) {
                        continue;
                    }
                    // limit to 30 results
                    if (30 < options_count) {
                        continue;
                    }
                }

                options_count += 1;
                
                // list entry
                ImGui::PushID(&option);
                ImGui::TableNextRow();

                // option name
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Indent(INDENT);
                if (option.is_active()) {
                    // active option
                    if (option.disabled || definition.disabled) {
                        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.f, 1.f), "%s", definition.title.c_str());
                    } else {
                        ImGui::TextColored(TEXT_COLOR_GREEN, "%s", definition.title.c_str());
                    }
                } else if (definition.hidden ||
                        (!definition.game_name.empty() && definition.game_name != this->games_selected_name)) {
                    // wrong game - grayed out
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "%s", definition.title.c_str());
                } else {
                    // normal text
                    ImGui::TextUnformatted(definition.title.c_str());
                }
                ImGui::Unindent(INDENT);
                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                    ImGui::HelpTooltip(definition.desc.c_str());
                }

                // command line parameter
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                std::string param = "-";
                if (definition.display_name.empty()) {
                    param += definition.name;
                } else {
                    param += definition.display_name;
                }
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "%s", param.c_str());
                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                    const auto help =
                        param +
                        "\n\nClick to copy the parameter to the clipboard.\n\n"
                        "These are the command-line parameters you can use in your .bat file to set the options.\n"
                        "Example: spice.exe -w -ea\n"
                        "         spice64.exe -api 1337 -apipass changeme";
                    ImGui::HelpTooltip(help.c_str());
                }
                if (ImGui::IsItemClicked()) {
                    clipboard::copy_text(param.c_str());
                }

                // option widgets
                ImGui::TableNextColumn();
                ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, 4.f);
                if (option.disabled || definition.disabled) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                switch (definition.type) {
                    case OptionType::Bool: {
                        bool state = !option.value.empty();
                        if (ImGui::Checkbox(state ? "ON" : "off", &state)) {
                            this->options_dirty = true;
                            option.value = state ? "/ENABLED" : "";
                            ::Config::getInstance().updateBinding(games_list[games_selected], option);
                        }
                        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    case OptionType::Integer: {
                        char buffer[512];
                        strncpy(buffer, option.value.c_str(), sizeof(buffer) - 1);
                        buffer[sizeof(buffer) - 1] = '\0';
                        auto digits_filter = [](ImGuiInputTextCallbackData* data) {
                            if ('0' <= data->EventChar && data->EventChar <= '9') {
                                return 0;
                            }
                            return 1; // discard
                        };

                        const char *hint = definition.setting_name.empty() ? "Enter number..."
                                : definition.setting_name.c_str();

                        ImGui::InputTextWithHint(
                            "", hint,
                            buffer, sizeof(buffer) - 1,
                            ImGuiInputTextFlags_CallbackCharFilter, digits_filter);
                        // would like to use IsItemDeactivatedAfterEdit but can't handle the case when window is closed while editing
                        if (ImGui::IsItemEdited()) {
                            this->options_dirty = true;
                            option.value = buffer;
                            ::Config::getInstance().updateBinding(games_list[games_selected], option);
                        }
                        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    case OptionType::Hex: {
                        char buffer[512];
                        strncpy(buffer, option.value.c_str(), sizeof(buffer) - 1);
                        buffer[sizeof(buffer) - 1] = '\0';
                        auto digits_filter = [](ImGuiInputTextCallbackData* data) {
                            if ('0' <= data->EventChar && data->EventChar <= '9') {
                                return 0;
                            }
                            if ('a' <= data->EventChar && data->EventChar <= 'f') {
                                return 0;
                            }
                            if ('A' <= data->EventChar && data->EventChar <= 'F') {
                                return 0;
                            }
                            if (data->EventChar == 'x' || data->EventChar == 'X') {
                                return 0;
                            }
                            return 1; // discard
                        };
                        const char *hint = definition.setting_name.empty() ? "Enter hex..."
                                : definition.setting_name.c_str();

                        ImGui::InputTextWithHint("", hint,
                            buffer, sizeof(buffer) - 1,
                            ImGuiInputTextFlags_CallbackCharFilter, digits_filter);
                        // would like to use IsItemDeactivatedAfterEdit but can't handle the case when window is closed while editing
                        if (ImGui::IsItemEdited()) {
                            this->options_dirty = true;
                            option.value = buffer;
                            ::Config::getInstance().updateBinding(games_list[games_selected], option);
                        }
                        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    case OptionType::Text: {
                        char buffer[OPTION_INPUT_TEXT_WIDTH];
                        strncpy(buffer, option.value.c_str(), sizeof(buffer) - 1);
                        buffer[sizeof(buffer) - 1] = '\0';

                        const char *hint = definition.setting_name.empty() ? "Enter value..."
                                : definition.setting_name.c_str();

                        ImGui::InputTextWithHint("", hint, buffer, sizeof(buffer) - 1);
                        // would like to use IsItemDeactivatedAfterEdit but can't handle the case when window is closed while editing
                        if (ImGui::IsItemEdited()) {
                            this->options_dirty = true;
                            option.value = buffer;
                            ::Config::getInstance().updateBinding(games_list[games_selected], option);
                        }
                        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    case OptionType::Enum: {
                        std::string current_item = option.value_text();
                        for (auto &element : definition.elements) {
                            if (element.first == current_item) {
                                if (!element.second.empty()) {
                                    current_item += fmt::format(" ({})", element.second);
                                }
                                break;
                            }
                        }
                        if (current_item.empty()) {
                            current_item = "Default";
                        }
                        if (ImGui::BeginCombo("##combo", current_item.c_str(), 0)) {
                            for (auto &element : definition.elements) {
                                std::string label = element.first;
                                if (!element.second.empty()) {
                                    label += fmt::format(" ({})", element.second);
                                }
                                
                                bool selected = current_item == label;
                                if (ImGui::Selectable(label.c_str(), selected)) {
                                    this->options_dirty = true;
                                    option.value = element.first;
                                    ::Config::getInstance().updateBinding(games_list[games_selected], option);
                                }
                                if (selected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    default: {
                        ImGui::Text("Unknown option type");
                        break;
                    }
                }

                // clear button
                if (!option.disabled && !definition.disabled && option.is_active() && option.get_definition().type != OptionType::Bool) {
                    ImGui::SameLine();
                    if (ImGui::ClearButton("Reset to default")) {
                        this->options_dirty = true;
                        option.value = "";
                        ::Config::getInstance().updateBinding(games_list[games_selected], option);
                    }
                }

                const std::string option_popup_id = build_option_value_picker_title(definition);
                if (definition.picker != OptionPickerType::None && !option.disabled && !definition.disabled) {
                    ImGui::SameLine();
                    if (ImGui::Button("Pick")) {
                        ImGui::OpenPopup(option_popup_id.c_str());
                    }
                    if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                        ImGui::HelpTooltip(definition.desc.c_str());
                    }
                }

                ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing

                // clean up disabled item flags
                if (option.disabled || definition.disabled) {
                    ImGui::PopItemFlag(); // ImGuiItemFlags_Disabled
                    ImGui::PopStyleVar(); // ImGuiStyleVar_Alpha
                }

                // disabled help
                if (option.disabled && !definition.disabled) {
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "This option cannot be edited because it was overriden by command-line options.\n"
                        "Run spicecfg.exe to configure the options and then run spice(64).exe directly.");
                }

                // value picker
                if (definition.picker != OptionPickerType::None &&
                    !option.disabled && !definition.disabled) {
                    if (ImGui::BeginPopupModal(option_popup_id.c_str(),
                            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        // for min width enforcement
                        ImGui::Dummy(ImVec2(320.f, 0.f));
                        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "%s", definition.title.c_str());

                        ImGui::TextUnformatted("");
                        
                        ImGui::TextUnformatted("Current value:");
                        ImGui::BeginDisabled();
                        // keeping it read only; if you want to make this editable, we need to refactor the
                        // input validation logic above (for int/hex) so people don't end up with invalid
                        // values that can crash spicecfg
                        ImGui::InputText("", &option.value, ImGuiInputTextFlags_ReadOnly);
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::ClearButton("Reset to default")) {
                            option.value = "";
                        }

                        ImGui::TextUnformatted("");

                        build_option_value_picker(option);

                        ImGui::TextUnformatted("");

                        if (ImGui::Button("Save & Close")) {
                            ImGui::CloseCurrentPopup();
                            this->options_dirty = true;
                            ::Config::getInstance().updateBinding(games_list[games_selected], option);  
                        }
                        ImGui::EndPopup();
                    }
                }

                // next item
                ImGui::PopID();
            }

            // check if empty
            if (options_count == 0) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(INDENT);
                ImGui::TextDisabled("-");
                ImGui::Unindent(INDENT);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }

        ImGui::TextUnformatted("");
    }

    void Config::build_about() {
#ifndef SPICE_LINUX
        ImGui::TextUnformatted(std::string(
            "spice2x (a fork of SpiceTools)\r\n"
            "=========================\r\n" +
            to_string(VERSION_STRING)).c_str());
#else
        ImGui::TextUnformatted(std::string(
            "spice2x (a fork of SpiceTools) for Linux\r\n"
            "=========================\r\n" +
            to_string(VERSION_STRING)).c_str());
#endif

        ImGui::TextUnformatted("");
        if (ImGui::TextLink(PROJECT_URL)) {
            launch_shell(PROJECT_URL);
        }
        
        ImGui::TextUnformatted("");
        ImGui::TextUnformatted(resutil::load_file_string_crlf(IDR_README).c_str());
        ImGui::TextUnformatted("");
        ImGui::TextUnformatted("");
        ImGui::TextUnformatted("Licenses:");
        ImGui::TextUnformatted(resutil::load_file_string_crlf(IDR_LICENSES).c_str());
    }

    void Config::build_launcher() {
        ImGui::TextUnformatted("Please select a game!");
        ImGui::Separator();
        ImGui::BeginChild("Launcher");
        this->build_about();
        ImGui::EndChild();
    }

    void Config::launch_shell(LPCSTR app, LPCSTR file) {
        // doing this on a separate thread to avoid polluting ImGui context
        std::thread t([app, file] {
            ShellExecuteA(NULL, "open", app, file, NULL, SW_SHOWNORMAL);
        });
        t.join();
    }

    void Config::build_menu(int *game_selected) {
        bool about_popup = false;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.14f, 0.14f, 0.14f, 1.0f));
        if (ImGui::BeginMenuBar()) {

            // [spice2x]
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
            if (ImGui::BeginMenu("[spice2x]")) {
                ImGui::PopStyleColor();
                if (ImGui::MenuItem("spice2x.github.io")) {
                    launch_shell(PROJECT_URL);
                }
                if (ImGui::MenuItem("About")) {
                    about_popup = true;
                }
                ImGui::EndMenu();
            } else {
                ImGui::PopStyleColor();
            }

            // shortcuts
            if (ImGui::BeginMenu("Shortcuts")) {
                if (ImGui::MenuItem("USB Game Controllers")) {
                    launch_shell("control.exe", "joy.cpl");
                }
                if (ImGui::MenuItem("Audio Playback Devices")) {
                    launch_shell("control.exe", "mmsys.cpl,,0");
                }
                if (ImGui::MenuItem("Tablet PC Settings")) {
                    launch_shell("explorer.exe", "shell:::{80F3F1D5-FECA-45F3-BC32-752C152E456E}");
                }
                ImGui::EndMenu();
            }

            // help
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("FAQ")) {
                    launch_shell("https://github.com/spice2x/spice2x.github.io/wiki/Known-issues");
                }
                if (ImGui::MenuItem("Wiki")) {
                    launch_shell("https://github.com/spice2x/spice2x.github.io/wiki");
                }
                ImGui::EndMenu();
            }

            // game selector
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.34f, 0.14f, 0.14f, 0.54f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.34f, 0.14f, 0.14f, 0.54f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.34f, 0.14f, 0.14f, 0.64f));
            ImGui::PushItemWidth(
                MIN(overlay::apply_scaling(700),
                    MAX(overlay::apply_scaling(100),
                        ImGui::GetWindowSize().x - overlay::apply_scaling(400))));

            ImGui::Combo("##game_selector", game_selected, games_names.data(), (int)games_list.size());
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(3);

            ImGui::BeginDisabled();
            if (!avs::game::is_model("000")) {
                ImGui::TextUnformatted(avs::game::get_identifier().c_str());
            }
            ImGui::EndDisabled();

            ImGui::EndMenuBar();
        }

        ImGui::PopStyleColor(); // ImGuiCol_PopupBg

        // workaround for popups triggered by menu, see https://github.com/ocornut/imgui/issues/331
        if (about_popup) {
            ImGui::OpenPopup("About##topbarpopup");
        }

        // draw popups
        {
            const ImVec2 popup_size(
                std::min(ImGui::GetIO().DisplaySize.x * 0.9f, overlay::apply_scaling(800.f)),
                std::min(ImGui::GetIO().DisplaySize.y * 0.9f, overlay::apply_scaling(800.f)));

            const ImVec2 popup_pos(
                ImGui::GetIO().DisplaySize.x / 2 - popup_size.x / 2,
                ImGui::GetIO().DisplaySize.y / 2 - popup_size.y / 2);
            
            ImGui::SetNextWindowSize(popup_size, ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(popup_pos, ImGuiCond_Appearing);
            bool unused_open2 = true;
            if (ImGui::BeginPopupModal("About##topbarpopup", &unused_open2)) {
                this->build_about();
                ImGui::EndPopup();
            }
        }
    }

    //////////////////
    /// Presets Tab //
    //////////////////

    ControllerTemplate Config::capture_current_bindings(const std::string &name) {
        ControllerTemplate tmpl;
        tmpl.name = name;
        tmpl.game_name = this->games_selected_name;
        tmpl.is_builtin = false;

        // capture buttons
        auto *buttons = games::get_buttons(this->games_selected_name);
        if (buttons) {
            for (auto &btn : *buttons) {
                tmpl.buttons.emplace_back(btn);
            }
        }

        // capture keypad buttons
        auto *keypad_buttons = games::get_buttons_keypads(this->games_selected_name);
        if (keypad_buttons) {
            for (auto &btn : *keypad_buttons) {
                tmpl.keypad_buttons.emplace_back(btn);
            }
        }

        // capture analogs
        auto *analogs = games::get_analogs(this->games_selected_name);
        if (analogs) {
            for (auto &a : *analogs) {
                tmpl.analogs.emplace_back(a);
            }
        }

        // capture lights
        auto *lights = games::get_lights(this->games_selected_name);
        if (lights) {
            for (auto &l : *lights) {
                TemplateLightBinding tl;
                tl.name = l.getName();
                tl.primary.device_identifier = l.getDeviceIdentifier();
                tl.primary.index = l.getIndex();

                for (auto &alt : l.getAlternatives()) {
                    TemplateLightEntry le;
                    le.device_identifier = alt.getDeviceIdentifier();
                    le.index = alt.getIndex();
                    tl.alternatives.push_back(le);
                }

                tmpl.lights.push_back(std::move(tl));
            }
        }

        return tmpl;
    }

    void Config::clear_all_bindings() {
        const auto &game = games_list[games_selected];

        // clear all game buttons (NOT overlay buttons)
        auto *buttons = games::get_buttons(this->games_selected_name);
        if (buttons) {
            for (auto &btn : *buttons) {
                for (int ai = (int)btn.getAlternatives().size() - 1; ai >= 0; ai--) {
                    clear_button(&btn.getAlternatives()[ai], ai + 1);
                }
                // clear primary
                clear_button(&btn, -1);
            }
        }

        // clear all keypad buttons
        auto *keypad_buttons = games::get_buttons_keypads(this->games_selected_name);
        if (keypad_buttons) {
            for (auto &btn : *keypad_buttons) {
                for (int ai = (int)btn.getAlternatives().size() - 1; ai >= 0; ai--) {
                    clear_button(&btn.getAlternatives()[ai], ai + 1);
                }
                clear_button(&btn, -1);
            }
        }

        // clear all analogs
        auto *analogs = games::get_analogs(this->games_selected_name);
        if (analogs) {
            for (auto &a : *analogs) {
                a.clearBindings();
                ::Config::getInstance().updateBinding(game, a);
            }
        }

        // clear all lights
        auto *lights = games::get_lights(this->games_selected_name);
        if (lights) {
            for (auto &l : *lights) {
                for (int ai = (int)l.getAlternatives().size() - 1; ai >= 0; ai--) {
                    clear_light(&l.getAlternatives()[ai], ai + 1);
                }
                clear_light(&l, -1);
            }
        }
    }

    // apply bindings from a template filtered by source, remapping to target_device
    void Config::apply_template_source(const ControllerTemplate &tmpl,
                                       const std::string &source_filter,
                                       const std::string &target_device) {
        const auto &game = games_list[games_selected];
        bool source_is_naive = (source_filter == "Naive");
        bool target_is_naive = (target_device == "Naive");

        // helper: check if a button entry matches the source filter
        auto entry_matches = [&](const TemplateButtonEntry &e) -> bool {
            if (source_is_naive) {
                return e.is_naive();
            }
            return e.is_device() && e.device_identifier == source_filter;
        };

        // helper: remap device identifier from source to target
        auto remap_devid = [&]() -> std::string {
            if (target_is_naive) {
                return "";
            }
            return target_device;
        };

        // helper: apply template button bindings to a game button vector
        auto apply_buttons = [&](const std::vector<TemplateButtonBinding> &tmpl_btns,
                                 std::vector<Button> *game_btns) {
            if (!game_btns) {
                return;
            }
            for (auto &tb : tmpl_btns) {
                std::vector<const TemplateButtonEntry *> matching_entries;
                if (entry_matches(tb.primary)) {
                    matching_entries.push_back(&tb.primary);
                }
                for (auto &alt : tb.alternatives) {
                    if (entry_matches(alt)) {
                        matching_entries.push_back(&alt);
                    }
                }
                if (matching_entries.empty()) {
                    continue;
                }

                for (auto &btn : *game_btns) {
                    if (btn.getName() != tb.name) {
                        continue;
                    }

                    for (auto *entry : matching_entries) {
                        auto mapped_devid = remap_devid();

                        if (btn.getVKey() == INVALID_VKEY && btn.getDeviceIdentifier().empty()) {
                            btn.setVKey(entry->vKey);
                            btn.setAnalogType(entry->analog_type);
                            btn.setDeviceIdentifier(mapped_devid);
                            btn.setInvert(entry->invert);
                            btn.setDebounceUp(entry->debounce_up);
                            btn.setDebounceDown(entry->debounce_down);
                            btn.setVelocityThreshold(entry->velocity_threshold);
                            ::Config::getInstance().updateBinding(game, btn, -1);
                        } else {
                            Button alt_btn(btn.getName());
                            alt_btn.setVKey(entry->vKey);
                            alt_btn.setAnalogType(entry->analog_type);
                            alt_btn.setDeviceIdentifier(mapped_devid);
                            alt_btn.setInvert(entry->invert);
                            alt_btn.setDebounceUp(entry->debounce_up);
                            alt_btn.setDebounceDown(entry->debounce_down);
                            alt_btn.setVelocityThreshold(entry->velocity_threshold);
                            alt_btn.setTemporary(true);
                            btn.getAlternatives().push_back(alt_btn);
                            ::Config::getInstance().updateBinding(
                                game, btn.getAlternatives().back(),
                                (int)btn.getAlternatives().size() - 1);
                            btn.getAlternatives().back().setTemporary(false);
                        }
                    }

                    break;
                }
            }
        };

        // apply game buttons and keypad buttons
        if (this->apply_buttons) {
            apply_buttons(tmpl.buttons, games::get_buttons(this->games_selected_name));
        }
        if (this->apply_keypads) {
            apply_buttons(tmpl.keypad_buttons, games::get_buttons_keypads(this->games_selected_name));
        }

        // apply analog bindings (device source only)
        if (this->apply_analogs && !source_is_naive && !target_is_naive) {
            auto *analogs = games::get_analogs(this->games_selected_name);
            if (analogs) {
                for (auto &ta : tmpl.analogs) {
                    if (!ta.is_device() || ta.device_identifier != source_filter) {
                        continue;
                    }

                    for (auto &a : *analogs) {
                        if (a.getName() != ta.name) {
                            continue;
                        }

                        a.setDeviceIdentifier(remap_devid());
                        a.setIndex(ta.index);
                        a.setSensitivity(ta.sensitivity);
                        a.setDeadzone(ta.deadzone);
                        a.setDeadzoneMirror(ta.deadzone_mirror);
                        a.setInvert(ta.invert);
                        a.setSmoothing(ta.smoothing);
                        a.setMultiplier(ta.multiplier);
                        a.setRelativeMode(ta.relative_mode);
                        a.setDelayBufferDepth(ta.delay_buffer_depth);
                        ::Config::getInstance().updateBinding(game, a);
                        break;
                    }
                }
            }
        }

        // apply light bindings (device source only)
        if (this->apply_lights && !source_is_naive && !target_is_naive) {
            auto *lights = games::get_lights(this->games_selected_name);
            if (lights) {
                for (auto &tl : tmpl.lights) {
                    // collect matching entries
                    std::vector<const TemplateLightEntry *> matching_entries;
                    if (tl.primary.is_device() && tl.primary.device_identifier == source_filter) {
                        matching_entries.push_back(&tl.primary);
                    }
                    for (auto &alt : tl.alternatives) {
                        if (alt.is_device() && alt.device_identifier == source_filter) {
                            matching_entries.push_back(&alt);
                        }
                    }
                    if (matching_entries.empty()) {
                        continue;
                    }

                    for (auto &l : *lights) {
                        if (l.getName() != tl.name) {
                            continue;
                        }

                        for (auto *entry : matching_entries) {
                            auto mapped_devid = remap_devid();
                            if (l.getDeviceIdentifier().empty()) {
                                // set as primary
                                l.setDeviceIdentifier(mapped_devid);
                                l.setIndex(entry->index);
                                ::Config::getInstance().updateBinding(game, l, -1);
                            } else {
                                // add as alternative
                                Light alt_light(l.getName());
                                alt_light.setDeviceIdentifier(mapped_devid);
                                alt_light.setIndex(entry->index);
                                alt_light.setTemporary(true);
                                l.getAlternatives().push_back(alt_light);
                                ::Config::getInstance().updateBinding(
                                    game, l.getAlternatives().back(),
                                    (int)l.getAlternatives().size() - 1);
                                l.getAlternatives().back().setTemporary(false);
                            }
                        }

                        break;
                    }
                }
            }
        }
    }

    void Config::build_presets() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Controller presets for: %s",
            this->games_selected_name.c_str());
        ImGui::Spacing();

        // refresh cache
        if (templates_cache_dirty) {
            templates_cache = get_templates_for_game(this->games_selected_name);
            templates_cache_dirty = false;
        }

        // preset list table
        if (!templates_cache.empty()) {
            if (ImGui::BeginTable("TemplateList", 6,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 3.f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 1.f);
                ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(60.f));
                ImGui::TableSetupColumn("Analogs", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(60.f));
                ImGui::TableSetupColumn("Lights", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(60.f));
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(140.f));
                ImGui::TableHeadersRow();

                for (int i = 0; i < (int)templates_cache.size(); i++) {
                    const auto &t = templates_cache[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    // name
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(t.name.c_str());

                    // type
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(t.is_builtin ? "Built-in" : "User");

                    // buttons
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", (int)t.buttons.size());

                    // analogs
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", (int)t.analogs.size());

                    // lights
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", (int)t.lights.size());

                    // actions
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Apply")) {
                        templates_selected = i;
                        template_target_selection.clear();
                        template_apply_open = true;
                    }
                    if (!t.is_builtin) {
                        ImGui::SameLine();
                        if (ImGui::Button("Edit")) {
                            templates_selected = i;
                            template_save_sources = t.get_binding_sources();
                            template_save_labels.resize(template_save_sources.size());
                            for (int si = 0; si < (int)template_save_sources.size(); si++) {
                                template_save_labels[si] = template_save_sources[si];
                            }
                            template_edit_open = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Del")) {
                            templates_selected = i;
                            template_delete_open = true;
                        }
                    }

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
            ImGui::Spacing();
        } else {
            ImGui::TextDisabled("No presets available for this game.");
        }

        // deferred popup opens
        if (template_apply_open) {
            ImGui::OpenPopup("Apply Preset##confirm");
            template_apply_open = false;
        }
        if (template_delete_open) {
            ImGui::OpenPopup("Delete Preset##confirm");
            template_delete_open = false;
        }

        // delete preset confirmation popup
        ImGui::SetNextWindowSize(ImVec2(360, 120), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Delete Preset##confirm", NULL, 0)) {
            if (templates_selected >= 0 && templates_selected < (int)templates_cache.size()) {
                const auto &t = templates_cache[templates_selected];
                ImGui::TextWrapped("Delete preset \"%s\"?", t.name.c_str());
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "This cannot be undone.");

                // buttons at the bottom
                ImGui::SetCursorPosY(ImGui::GetWindowHeight()
                    - ImGui::GetFrameHeightWithSpacing()
                    - ImGui::GetStyle().WindowPadding.y);
                if (ImGui::Button("Delete")) {
                    delete_user_template(t.game_name, t.name);
                    templates_cache_dirty = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        // apply preset popup
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Apply Preset##confirm", NULL, 0)) {

            if (templates_selected >= 0 && templates_selected < (int)templates_cache.size()) {
                const auto &t = templates_cache[templates_selected];

                ImGui::Text("Applying preset: \"%s\"", t.name.c_str());
                ImGui::Spacing();

                // clear all button
                ImGui::Separator();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f),
                    "Step 1: Reset all existing bindings (Caution! This can't be undone.)");
                ImGui::BeginDisabled(this->all_cleared);
                if (ImGui::Button("Clear All Bindings")) {
                    clear_all_bindings();
                    this->all_cleared = true;
                    std::fill(this->template_is_applied.begin(), this->template_is_applied.end(), false);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::HelpMarker(
                    "Clears all game button, analog, and light bindings.");
                if (this->all_cleared) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("Done.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1),
                    "Step 2: Pick which groups to apply");
                ImGui::Spacing();
                bool selection_changed = false;
                selection_changed |= ImGui::Checkbox("Buttons", &this->apply_buttons);
                ImGui::SameLine();
                selection_changed |= ImGui::Checkbox("Keypads", &this->apply_keypads);
                ImGui::SameLine();
                selection_changed |= ImGui::Checkbox("Analogs", &this->apply_analogs);
                ImGui::SameLine();
                selection_changed |= ImGui::Checkbox("Lights", &this->apply_lights);
                if (selection_changed) {
                    std::fill(this->template_is_applied.begin(), this->template_is_applied.end(), false);
                }

                if (!this->apply_buttons && !this->apply_keypads &&
                    !this->apply_analogs && !this->apply_lights) {
                    ImGui::TextUnformatted("\nYou must select at least one group to apply.\n\n");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Step 3: Pick devices to apply profiles");
                ImGui::Spacing();
                ImGui::TextUnformatted("Hint: hold a button on the controller, it'll turn green in the drop down.");
                ImGui::Spacing();

                // list binding sources with target selection
                const auto sources = t.get_binding_sources();
                if (sources.size() != template_save_sources.size()) {
                    this->template_is_applied.resize(sources.size(), false);
                }

                // build target device list: "Naive" + all connected devices
                std::vector<std::string> target_options;
                target_options.emplace_back("Naive");
                for (auto &device : RI_MGR->devices_get()) {
                    target_options.push_back(device.name);
                }

                // ensure target selection vector is sized (reset on source count change)
                if (template_target_selection.size() != sources.size()) {
                    template_target_selection.resize(sources.size());
                    for (int si = 0; si < (int)sources.size(); si++) {
                        template_target_selection[si] = -1;
                        for (int ti = 0; ti < (int)target_options.size(); ti++) {
                            if (target_options[ti] == sources[si]) {
                                template_target_selection[si] = ti;
                                break;
                            }
                        }
                    }
                }

                if (!apply_buttons && !apply_keypads && !apply_analogs && !apply_lights) {
                    ImGui::BeginDisabled();
                }

                if (ImGui::BeginTable("TemplateSources", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                    ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_None, 2.f);
                    ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_None, 3.f);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, overlay::apply_scaling(80));
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_None, 1.f);
                    ImGui::TableHeadersRow();

                    // build a set of device names with any button currently pressed
                    std::set<std::string> active_devices;
                    for (auto &dev : RI_MGR->devices_get()) {
                        bool pressed = false;
                        switch (dev.type) {
                            case rawinput::HID:
                                if (dev.hidInfo) {
                                    for (auto &page : dev.hidInfo->button_states) {
                                        for (bool s : page) {
                                            if (s) {
                                                pressed = true;
                                                break;
                                            }
                                        }
                                        if (pressed) {
                                            break;
                                        }
                                    }
                                }
                                break;
                            case rawinput::KEYBOARD:
                                if (dev.keyboardInfo) {
                                    for (bool s : dev.keyboardInfo->key_states) {
                                        if (s) {
                                            pressed = true;
                                            break;
                                        }
                                    }
                                }
                                break;
                            case rawinput::MOUSE:
                                if (dev.mouseInfo) {
                                    for (bool s : dev.mouseInfo->key_states) {
                                        if (s) {
                                            pressed = true;
                                            break;
                                        }
                                    }
                                }
                                break;
                            case rawinput::MIDI:
                                if (dev.midiInfo) {
                                    for (bool s : dev.midiInfo->states) {
                                        if (s) {
                                            pressed = true;
                                            break;
                                        }
                                    }
                                }
                                break;
                            default:
                                break;
                        }
                        if (pressed) {
                            active_devices.insert(dev.name);
                        }
                    }

                    for (int si = 0; si < (int)sources.size(); si++) {
                        ImGui::PushID(si);
                        ImGui::TableNextRow();

                        // source (label is the device_identifier)
                        // highlight if the selected target device has recent input
                        ImGui::TableNextColumn();
                        {
                            int sel = template_target_selection[si];
                            bool active = sel > 0 && active_devices.count(target_options[sel]) > 0;
                            ImGui::AlignTextToFramePadding();
                            if (active) {
                                ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "%s", sources[si].c_str());
                            } else {
                                ImGui::TextUnformatted(sources[si].c_str());
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", t.get_source_summary(sources[si]).c_str());
                        }

                        // target device combo
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-1);
                        {
                            const int sel = template_target_selection[si];
                            const char *preview = (sel >= 0) ? target_options[sel].c_str() : "(select target)";
                            if (ImGui::BeginCombo("##target", preview)) {
                                for (int ti = 0; ti < (int)target_options.size(); ti++) {
                                    bool is_selected = (sel == ti);
                                    std::string combo_label = target_options[ti];
                                    bool item_active = false;
                                    if (ti > 0) {
                                        auto *dev = RI_MGR->devices_get(target_options[ti]);
                                        if (dev) {
                                            combo_label = fmt::format("{} ({})##{}", dev->desc.substr(0, 42), dev->name.substr(0, 36), dev->name);
                                            item_active = active_devices.count(dev->name) > 0;
                                        }
                                    }
                                    if (item_active) {
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                                    }
                                    if (ImGui::Selectable(combo_label.c_str(), is_selected)) {
                                        template_target_selection[si] = ti;
                                    }
                                    if (item_active) {
                                        ImGui::PopStyleColor();
                                    }
                                    if (is_selected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                this->template_is_applied[si] = false;
                                ImGui::EndCombo();
                            }
                        }

                        // connection status of target
                        ImGui::TableNextColumn();
                        {
                            int sel = template_target_selection[si];
                            if (sel < 0) {
                                ImGui::TextDisabled("--");
                            } else if (this->template_is_applied[si]) {
                                ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "Applied!");
                            } else if (target_options[sel] == "Naive") {
                                ImGui::TextUnformatted("Ready");
                            } else {
                                auto *device = RI_MGR->devices_get(target_options[sel]);
                                if (device) {
                                    ImGui::TextUnformatted("Ready");
                                } else {
                                    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.f, 1.f), "Not found");
                                }
                            }
                        }

                        // apply button
                        ImGui::TableNextColumn();
                        {
                            const int sel = template_target_selection[si];
                            ImGui::BeginDisabled(sel < 0 || this->template_is_applied[si]);
                            if (ImGui::Button("Apply")) {
                                if (sel >= 0) {
                                    apply_template_source(t, sources[si], target_options[sel]);
                                    this->template_is_applied[si] = true;
                                    this->all_cleared = false;
                                }
                            }
                            ImGui::EndDisabled();
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            
                if (!apply_buttons && !apply_keypads && !apply_analogs && !apply_lights) {
                    ImGui::EndDisabled();
                }
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight()
                - ImGui::GetFrameHeightWithSpacing()
                - ImGui::GetStyle().WindowPadding.y);
            if (ImGui::Button("Close")) {
                templates_selected = -1;
                all_cleared = false;
                template_target_selection.clear();
                std::fill(this->template_is_applied.begin(), this->template_is_applied.end(), false);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // save section
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Save Current Bindings as Preset");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Preset Name", template_save_name, sizeof(template_save_name));
        ImGui::SameLine();
        bool name_empty = strlen(template_save_name) == 0;
        if (name_empty) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save")) {
            // capture bindings and open label assignment dialog
            template_pending_save = capture_current_bindings(template_save_name);
            template_save_sources = template_pending_save.get_binding_sources();
            template_save_labels.resize(template_save_sources.size());
            for (int si = 0; si < (int)template_save_sources.size(); si++) {
                // default label: device description or source id
                const auto &src = template_save_sources[si];
                if (src == "Naive") {
                    template_save_labels[si] = "Keyboard";
                } else {
                    auto *dev = RI_MGR->devices_get(src);
                    template_save_labels[si] = dev ? dev->desc : src;
                }
            }
            template_save_labels_open = true;
        }
        if (name_empty) {
            ImGui::EndDisabled();
        }

        // open save labels popup (deferred)
        if (template_save_labels_open) {
            ImGui::OpenPopup("Assign Labels##save");
            template_save_labels_open = false;
        }

        // open edit labels popup (deferred from Edit button inside table)
        if (template_edit_open) {
            ImGui::OpenPopup("Edit Labels##preset");
            template_edit_open = false;
        }

        // save labels popup: assign labels to sources before saving
        ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Assign Labels##save", NULL, 0)) {

            ImGui::TextUnformatted("Assign a label to each source device.");
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1),
                "Labels replace device IDs in the saved preset.");
            ImGui::Spacing();

            ImGui::TextUnformatted("Pick which groups to save:");
            ImGui::Checkbox("Buttons", &this->save_buttons);
            ImGui::SameLine();
            ImGui::Checkbox("Keypads", &this->save_keypads);
            ImGui::SameLine();
            ImGui::Checkbox("Analogs", &this->save_analogs);
            ImGui::SameLine();
            ImGui::Checkbox("Lights", &this->save_lights);
            if (!this->save_analogs && !this->save_keypads &&
                !this->save_analogs && !this->save_lights) {
                ImGui::TextUnformatted("\nYou must select at least one group to save.\n\n");
            }

            if (ImGui::BeginTable("SaveLabels", 2,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_None, 3.f);
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_None, 3.f);
                ImGui::TableHeadersRow();

                for (int si = 0; si < (int)template_save_sources.size(); si++) {
                    ImGui::PushID(si + 3000);
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(template_save_sources[si].c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s",
                            template_pending_save.get_source_summary(
                                template_save_sources[si]).c_str());
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    char buf[128] = {};
                    strncpy(buf, template_save_labels[si].c_str(), sizeof(buf) - 1);
                    if (ImGui::InputText("##label", buf, sizeof(buf))) {
                        template_save_labels[si] = buf;
                    }

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            ImGui::Spacing();

            // check all labels are non-empty
            bool all_labels_set = true;
            for (auto &lbl : template_save_labels) {
                if (lbl.empty()) { all_labels_set = false; break; }
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight()
                - ImGui::GetFrameHeightWithSpacing()
                - ImGui::GetStyle().WindowPadding.y);

            ImGui::BeginDisabled(
                !all_labels_set ||
                (!this->save_buttons && !this->save_keypads && !this->save_analogs && !this->save_lights));

            if (ImGui::Button("Save")) {
                // replace device IDs with labels in the template
                for (int si = 0; si < (int)template_save_sources.size(); si++) {
                    template_pending_save.rename_source(
                        template_save_sources[si], template_save_labels[si]);
                }
                if (save_user_template(template_pending_save,
                        this->save_buttons, this->save_keypads, this->save_analogs, this->save_lights)) {
                    template_save_name[0] = '\0';
                    templates_cache_dirty = true;
                }
                template_save_sources.clear();
                template_save_labels.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                template_save_sources.clear();
                template_save_labels.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // edit labels popup: rename sources in an existing preset
        ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Edit Labels##preset", NULL, 0)) {

            if (templates_selected >= 0 && templates_selected < (int)templates_cache.size()) {
                auto &t = templates_cache[templates_selected];
                ImGui::Text("Edit labels for \"%s\"", t.name.c_str());
                ImGui::Spacing();

                if (ImGui::BeginTable("EditLabels", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                    ImGui::TableSetupColumn("Current Label", ImGuiTableColumnFlags_None, 3.f);
                    ImGui::TableSetupColumn("New Label", ImGuiTableColumnFlags_None, 3.f);
                    ImGui::TableHeadersRow();

                    for (int si = 0; si < (int)template_save_sources.size(); si++) {
                        ImGui::PushID(si + 2000);
                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(template_save_sources[si].c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s",
                                t.get_source_summary(template_save_sources[si]).c_str());
                        }

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-1);
                        char buf[128] = {};
                        strncpy(buf, template_save_labels[si].c_str(), sizeof(buf) - 1);
                        if (ImGui::InputText("##label", buf, sizeof(buf))) {
                            template_save_labels[si] = buf;
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }

                ImGui::Spacing();

                // check all labels are non-empty
                bool all_labels_set = true;
                for (auto &lbl : template_save_labels) {
                    if (lbl.empty()) { all_labels_set = false; break; }
                }

                ImGui::SetCursorPosY(ImGui::GetWindowHeight()
                    - ImGui::GetFrameHeightWithSpacing()
                    - ImGui::GetStyle().WindowPadding.y);
                if (!all_labels_set) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Save Labels")) {
                    // rename sources in the template
                    for (int si = 0; si < (int)template_save_sources.size(); si++) {
                        if (template_save_labels[si] != template_save_sources[si]) {
                            t.rename_source(template_save_sources[si], template_save_labels[si]);
                        }
                    }
                    save_user_template(t, true, true, true, true);
                    templates_cache_dirty = true;
                    template_save_sources.clear();
                    template_save_labels.clear();
                    templates_selected = -1;
                    ImGui::CloseCurrentPopup();
                }
                if (!all_labels_set) {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    template_save_sources.clear();
                    template_save_labels.clear();
                    templates_selected = -1;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }

    void Config::set_alternating_row_colors(const int row_index) {
        auto color = ImGuiCol_TableRowBg;
        if (row_index % 2 != 0) {
            color = ImGuiCol_TableRowBgAlt;
        }
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(color));
    }
}
