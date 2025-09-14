#include "config.h"

#include <thread>

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>

#include "build/defs.h"
#include "build/resource.h"
#include "cfg/config.h"
#include "cfg/configurator.h"
#include "external/imgui/imgui_internal.h"
#include "games/io.h"
#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
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
#include "util/time.h"
#include "util/utils.h"

#ifdef min
#undef min
#endif

namespace overlay::windows {

    // same width as dummy marker
    const float INDENT = 22.f;
    const auto PROJECT_URL = "https://spice2x.github.io";

    Config::Config(overlay::SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Configuration";
        this->toggle_button = games::OverlayButtons::ToggleConfig;
        this->init_size = ImVec2(800, 600);
        this->size_min = ImVec2(100, 200);
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
                auto file_hints = games::get_game_file_hints(game_name);
                if (file_hints) {
                    for (auto &file_hint : *file_hints) {
                        if (fileutils::file_exists(file_hint) ||
                            fileutils::file_exists(std::filesystem::path("modules") / file_hint) ||
                            fileutils::file_exists(std::filesystem::path("contents") / file_hint) ||
                            fileutils::file_exists(MODULE_PATH / file_hint))
                        {
                            this->games_selected = games_list.size() - 1;
                            this->games_selected_name = game_name;
                            eamuse_set_game(game_name);
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

    void Config::write_card(int player) {

        // get path
        auto bindings = ::Config::getInstance().getKeypadBindings(this->games_selected_name);
        std::filesystem::path path;
        if (!bindings.card_paths[player].empty()) {
            path = bindings.card_paths[player];
        } else {
            path = player > 0 ? "card1.txt" : "card0.txt";
        }

        // write file
        std::ofstream f(path);
        if (f) {
            f.write(this->keypads_card_number[player], strlen(this->keypads_card_number[player]));
            f.close();
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
        }

        // tab selection
        auto tab_selected_new = ConfigTab::CONFIG_TAB_INVALID;
        if (ImGui::BeginTabBar("Config Tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
            const int page_offset = cfg::CONFIGURATOR_STANDALONE ? 88 : 110;
            const int page_offset2 = cfg::CONFIGURATOR_STANDALONE ? 65 : 87;

            if (ImGui::BeginTabItem("Buttons")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_BUTTONS;
                ImGui::BeginChild("Buttons", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset), false);

                // help text for binding buttons, if the game has one
                const auto help_text = games::get_buttons_help(this->games_selected_name);
                if (!help_text.empty()) {
                    ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Button Layout");
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

                // page selector
                this->build_page_selector(&this->buttons_page);

                // bind many
                ImGui::SameLine();
                if (ImGui::Checkbox("Bind Many", &buttons_many_active)) {
                    buttons_many_index = -1;
                    buttons_many_delay = 0;
                }
                ImGui::SameLine();
                ImGui::HelpMarker("Immediately query for the next button after binding one.");

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Analogs")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_ANALOGS;
                ImGui::BeginChild("Analogs", ImVec2(
                    0, ImGui::GetWindowContentRegionMax().y - page_offset2), false);
                this->build_analogs("Game", games::get_analogs(this->games_selected_name));
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Overlay")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_OVERLAY;
                ImGui::BeginChild("Overlay", ImVec2(
                    0, ImGui::GetWindowContentRegionMax().y - page_offset), false);

                // overlay buttons
                this->build_buttons("Overlay", games::get_buttons_overlay(this->games_selected_name));
                ImGui::EndChild();

                // page selector
                this->build_page_selector(&this->buttons_page);

                // standalone configurator extras
                if (cfg::CONFIGURATOR_STANDALONE) {
                    ImGui::SameLine();
                    ImGui::Checkbox("Enable Overlay in Config", &OVERLAY->hotkeys_enable);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Lights")) {
                tab_selected_new = ConfigTab::CONFIG_TAB_LIGHTS;
                ImGui::BeginChild("Lights", ImVec2(
                        0, ImGui::GetWindowContentRegionMax().y - page_offset), false);
                this->build_lights("Game", games::get_lights(this->games_selected_name));
                ImGui::EndChild();
                this->build_page_selector(&this->lights_page);
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
            this->tab_selected = tab_selected_new;
            this->buttons_page = 0;
            this->lights_page = 0;
            buttons_many_active = false;
            buttons_many_index = -1;
            ImGui::CloseCurrentPopup();
        }

        // disclaimer
        ImGui::TextColored(
                ImVec4(1, 0.5f, 0.5f, 1.f),
                "Do NOT stream or upload game data anywhere public! Support arcades when you can. Thanks.");
    }

    void Config::build_buttons(const std::string &name, std::vector<Button> *buttons, int min, int max) {

        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "%s Buttons", name.c_str());
        ImGui::Separator();
        if (ImGui::BeginTable("ButtonsTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 240);

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
                int button_it_max = max < 0 ? buttons->size() - 1 : std::min((int) buttons->size() - 1, max);
                for (int button_it = min; button_it <= button_it_max; button_it++) {
                    auto &button_in = buttons->at(button_it);

                    // get button based on page
                    auto button = &button_in;
                    auto &button_alternatives = button->getAlternatives();
                    if (this->buttons_page > 0) {
                        while ((int) button_alternatives.size() < this->buttons_page) {
                            button_alternatives.emplace_back(button->getName());
                        }
                        button = &button_alternatives.at(this->buttons_page - 1);
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    // get button info
                    ImGui::PushID(button);
                    auto button_name = button->getName();
                    auto button_display = button->getDisplayString(RI_MGR.get());
                    auto button_state = GameAPI::Buttons::getState(RI_MGR, *button);
                    auto button_velocity = GameAPI::Buttons::getVelocity(RI_MGR, *button);

                    // list entry
                    bool style_color = false;
                    if (button_state == GameAPI::Buttons::State::BUTTON_PRESSED) {
                        style_color = true;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.7f, 0.f, 1.f));
                    } else if (button_display.empty()) {
                        style_color = true;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.f));
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::Indent(INDENT);
                    ImGui::Text("%s", button_name.c_str());
                    ImGui::Unindent(INDENT);

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", button_display.empty() ? "-" : button_display.c_str());

                    ImGui::TableNextColumn();
                    if (style_color) {
                        ImGui::PopStyleColor();
                    }

                    // normal button binding
                    std::string bind_name = "Bind " + button_name;
                    if (ImGui::Button("Bind")
                    || (buttons_many_active && buttons_many_active_section == name && !buttons_bind_active
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
                    if (ImGui::IsItemHovered()) {
                        ImGui::HelpTooltip(
                            "Use 'Bind' to bind a button to a device using RawInput.\n"
                            "Use 'Naive' for device independent binding using GetAsyncKeyState.");
                    }

                    if (ImGui::BeginPopupModal(bind_name.c_str(), NULL,
                            ImGuiWindowFlags_AlwaysAutoResize)) {

                        // modal content
                        ImGui::Text("Please press any button.");
                        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Hint: Press ESC to cancel!");
                        if (ImGui::Button("Cancel")) {
                            RI_MGR->devices_midi_freeze(false);
                            buttons_bind_active = false;
                            buttons_many_index = -1;
                            ImGui::CloseCurrentPopup();
                        } else {

                            // iterate updated devices
                            auto updated_devices = RI_MGR->devices_get_updated();
                            for (auto device : updated_devices) {
                                std::lock_guard<std::mutex> lock(*device->mutex);
                                switch (device->type) {
                                    case rawinput::MOUSE: {
                                        auto mouse = device->mouseInfo;
                                        for (size_t i = 0; i < sizeof(mouse->key_states_bind); i++) {
                                            if (mouse->key_states[i] && !mouse->key_states_bind[i]
                                            && !ImGui::IsItemHovered()) {

                                                // bind key
                                                button->setDeviceIdentifier(device->name);
                                                button->setVKey(static_cast<unsigned short>(i));
                                                button->setAnalogType(BAT_NONE);
                                                ::Config::getInstance().updateBinding(
                                                        games_list[games_selected], *button, buttons_page - 1);
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
                                                    RI_MGR->devices_midi_freeze(false);
                                                    break;
                                                }

                                                // bind key
                                                button->setDeviceIdentifier(device->name);
                                                button->setVKey(vkey);
                                                button->setAnalogType(BAT_NONE);
                                                ::Config::getInstance().updateBinding(
                                                        games_list[games_selected], *button, buttons_page - 1);
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
                                                                buttons_page - 1);
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
                                                            buttons_page - 1);
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
                                                            buttons_page - 1);
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
                                                        buttons_page - 1);
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
                                                            buttons_page - 1);
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
                                                            buttons_page - 1);
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
                                                            buttons_page - 1);
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
                                                        buttons_page - 1);
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
                                                        buttons_page - 1);
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
                                                        buttons_page - 1);
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

                    // naive binding
                    ImGui::SameLine();
                    std::string naive_string = "Naive " + button_name;
                    if (ImGui::Button("Naive")
                    || (buttons_many_active && buttons_many_active_section == name && !buttons_bind_active
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
                    if (ImGui::IsItemHovered()) {
                        ImGui::HelpTooltip(
                            "Use 'Bind' to bind a button to a device using RawInput.\n"
                            "Use 'Naive' for device independent binding using GetAsyncKeyState.");
                    }

                    if (ImGui::BeginPopupModal(naive_string.c_str(), NULL,
                            ImGuiWindowFlags_AlwaysAutoResize)) {
                        buttons_bind_active = true;

                        // modal content
                        ImGui::Text("Please press any button.");
                        const bool escape_cancels_bind = (this->tab_selected != ConfigTab::CONFIG_TAB_OVERLAY);
                        if (escape_cancels_bind) {
                            ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Hint: Press ESC to cancel!");
                        }
                        if (ImGui::Button("Cancel")) {
                            buttons_bind_active = false;
                            buttons_many_index = -1;
                            ImGui::CloseCurrentPopup();
                        } else {

                            // get new keyboard state
                            bool keyboard_state_new[sizeof(buttons_keyboard_state)];
                            for (size_t i = 0; i < sizeof(keyboard_state_new); i++) {
                                keyboard_state_new[i] = GetAsyncKeyState(i) != 0;
                            }

                            // detect key presses
                            for (unsigned short int vKey = 0x01; vKey < sizeof(buttons_keyboard_state); vKey++) {

                                // ignore num lock escape sequence
                                if (vKey != VK_NUMLOCK) {

                                    // check for key press
                                    if (keyboard_state_new[vKey] && !buttons_keyboard_state[vKey]) {

                                        // ignore escape
                                        if ((!escape_cancels_bind || vKey != VK_ESCAPE) &&
                                            (vKey != VK_LBUTTON || !ImGui::IsItemHovered())) {

                                            // bind key
                                            button->setDeviceIdentifier("");
                                            button->setVKey(vKey);
                                            button->setDebounceUp(0.0);
                                            button->setDebounceDown(0.0);
                                            button->setVelocityThreshold(0);
                                            ::Config::getInstance().updateBinding(
                                                    games_list[games_selected], *button,
                                                    buttons_page - 1);
                                            inc_buttons_many_index(button_it_max);
                                        } else {
                                            buttons_many_index = -1;
                                        }

                                        buttons_bind_active = false;
                                        ImGui::CloseCurrentPopup();
                                    }
                                }
                            }

                            // clean up
                            memcpy(buttons_keyboard_state, keyboard_state_new, sizeof(buttons_keyboard_state));
                        }
                        ImGui::EndPopup();
                    }

                    // edit button
                    ImGui::SameLine();
                    std::string edit_name = "Edit " + button->getName();
                    if (ImGui::Button("Edit")) {
                        ImGui::OpenPopup(edit_name.c_str());
                    }
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
                            ImGui::Text("%s\n\n", button_display.c_str());
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
                                    games_list[games_selected], *button, buttons_page - 1);
                        }

                        // close button
                        ImGui::TextUnformatted("");
                        if (ImGui::Button("Save & Close")) {
                            buttons_many_active = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    // clear button
                    if (button_display.size() > 0) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            button->setDeviceIdentifier("");
                            button->setVKey(0xFF);
                            button->setAnalogType(BAT_NONE);
                            button->setDebounceUp(0.0);
                            button->setDebounceDown(0.0);
                            button->setVelocityThreshold(0);
                            button->setInvert(false);
                            button->setLastState(GameAPI::Buttons::BUTTON_NOT_PRESSED);
                            button->setLastVelocity(0);
                            ::Config::getInstance().updateBinding(
                                    games_list[games_selected], *button,
                                    buttons_page - 1);
                        }
                    }

                    // clean up
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }

    void Config::build_analogs(const std::string &name, std::vector<Analog> *analogs) {
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Analogs");
        ImGui::Separator();
        if (ImGui::BeginTable("AnalogsTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 240);

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

                    // list entry
                    ImGui::ProgressBar(analog_state, ImVec2(32.f, 0));
                    ImGui::SameLine();
                    if (analog_display.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.f));
                    }
                    ImGui::Text("%s", analog_name.c_str());

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", analog_display.empty() ? "-" : analog_display.c_str());

                    ImGui::TableNextColumn();
                    if (analog_display.empty()) {
                        ImGui::PopStyleColor();
                    }

                    // analog binding
                    if (ImGui::Button("Bind")) {
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
                    if (ImGui::BeginPopupModal("Analog Binding", NULL,
                            ImGuiWindowFlags_AlwaysAutoResize)) {

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

                        // clean up
                        ImGui::EndPopup();
                    }

                    // clear analog
                    if (analog_display.size() > 0) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            analog.clearBindings();
                            analog.setLastState(0.f);
                            ::Config::getInstance().updateBinding(
                                    games_list[games_selected], analog);
                        }
                    }

                    // clean up
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }

    void Config::build_lights(const std::string &name, std::vector<Light> *lights) {
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Lights");
        ImGui::Separator();
        if (ImGui::BeginTable("LightsTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 240);

            // check if empty
            if (!lights || lights->empty()) {
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

            // check lights
            if (lights) {
                for (auto &light_in : *lights) {

                    // get light based on page
                    auto light = &light_in;
                    auto &light_alternatives = light->getAlternatives();
                    if (this->lights_page > 0) {
                        while ((int) light_alternatives.size() < this->lights_page) {
                            light_alternatives.emplace_back(light->getName());
                        }
                        light = &light_alternatives.at(this->lights_page - 1);
                    }

                    // get light info
                    auto light_name = light->getName();
                    auto light_display = light->getDisplayString(RI_MGR.get());
                    auto light_state = GameAPI::Lights::readLight(RI_MGR, *light);

                    // list entry
                    if (light_display.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.f));
                    }
                    ImGui::PushID(light);
                    ImGui::TableNextRow();

                    // progress bar & light name
                    ImGui::TableNextColumn();
                    ImGui::ProgressBar(light_state, ImVec2(32.f, 0));
                    ImGui::SameLine();
                    ImGui::Text("%s", light_name.c_str());

                    // binding name
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", light_display.empty() ? "-" : light_display.c_str());

                    // bind / clear buttons
                    ImGui::TableNextColumn();
                    if (light_display.empty()) {
                        ImGui::PopStyleColor();
                    }

                    // light binding
                    if (ImGui::Button("Bind")) {
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
                    if (ImGui::BeginPopupModal("Light Binding", NULL,
                            ImGuiWindowFlags_AlwaysAutoResize)) {

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
                                        lights_page - 1);
                            }
                            if (this->lights_devices_control_selected >= 0) {
                                if ((int) light->getIndex() != this->lights_devices_control_selected) {
                                    light->setIndex(this->lights_devices_control_selected);
                                    ::Config::getInstance().updateBinding(
                                            games_list[games_selected], *light,
                                            lights_page - 1);
                                }
                            }
                        }

                        // value preview
                        ImGui::Separator();
                        float value_orig = GameAPI::Lights::readLight(RI_MGR, *light);
                        float value = value_orig;
                        ImGui::SliderFloat("Preview", &value, 0.f, 1.f);

                        // manual button controls
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
                            ImGui::Checkbox("Lock", &light->override_enabled);
                        }

                        // apply new value
                        if (value != value_orig || control_changed) {
                            if (light->override_enabled) {
                                light->override_state = value;
                            }
                            auto ident = light->getDeviceIdentifier();
                            GameAPI::Lights::writeLight(RI_MGR->devices_get(ident), light->getIndex(), value);
                            RI_MGR->devices_flush_output();
                        }

                        // close button
                        ImGui::Separator();
                        if (ImGui::Button("Close")) {
                            ImGui::CloseCurrentPopup();
                            light->override_enabled = false;
                        }

                        // clean up
                        ImGui::EndPopup();
                    }

                    // clear light
                    if (light_display.size() > 0) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) {
                            light->setDeviceIdentifier("");
                            light->setIndex(0xFF);
                            ::Config::getInstance().updateBinding(
                                    games_list[games_selected], *light,
                                    lights_page - 1);
                        }
                    }

                    // clean up
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }

    void Config::build_cards() {

        // early quit
        if (this->games_selected < 0 || this->games_selected_name.empty()) {
            ImGui::Text("Please select a game first.");
            return;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Card files (card0.txt / card1.txt)");
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

            // custom ID and title
            ImGui::PushID(("KeypadP" + to_string(player)).c_str());
            ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Player %i", player + 1);

            // card path
            std::string hint = "card" + to_string(player) + ".txt";
            if (ImGui::InputTextWithHint("Card Path", hint.c_str(),
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
                        OPENFILENAMEW ofn {};
                        memset(&ofn, 0, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = nullptr;
                        ofn.lpstrFilter = L"";
                        ofn.lpstrFile = ofn_path.get();
                        ofn.nMaxFile = 512;
                        ofn.Flags = OFN_EXPLORER;
                        ofn.lpstrDefExt = L"txt";

                        // check for success
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

            // clear button
            if (!bindings.card_paths[player].empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    bindings.card_paths[player] = "";
                    bindings_updated = true;
                }
            }

            // verify card number
            auto card_valid = true;
            if (this->keypads_card_number[player][0] != 0) {
                for (int n = 0; n < 16; n++) {
                    char c = this->keypads_card_number[player][n];
                    bool digit = c >= '0' && c <= '9';
                    bool character_big = c >= 'A' && c <= 'F';
                    bool character_small = c >= 'a' && c <= 'f';
                    if (!digit && !character_big && !character_small) {
                        card_valid = false;
                    }
                }
            }

            // card number box
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  card_valid ? ImVec4(1.f, 1.f, 1.f, 1.f) :
                                  ImVec4(1.f, 0.f, 0.f, 1.f));
            ImGui::InputTextWithHint("Card Number", "E004010000000000",
                    this->keypads_card_number[player], sizeof(this->keypads_card_number[0]) - 1,
                    ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopStyleColor();

            // write card after edit
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                write_card(player);
                read_card(1 - player);
            }

            // help marker
            ImGui::SameLine();
            ImGui::HelpMarker(
                "Click on Generate button to randomize a valid card number and automatically it save to specified file.");

            // generate button
            ImGui::SameLine();
            if (ImGui::Button("Generate")) {
                // don't know why this file insists on using 18 chars to store the card ID
                char new_card[17];
                generate_ea_card(new_card);
                strcpy_s(this->keypads_card_number[player], new_card);
                write_card(player);
                read_card(1 - player);
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
        ImGui::SeparatorText("NFC card reader status");
        ImGui::Spacing();
        if (cfg::CONFIGURATOR_STANDALONE) {

            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("Test NFC card readers and card insertions over API");
            ImGui::SameLine();
            ImGui::HelpMarker(
                "Enable card readers in Advanced tab, under Card Readers section, and restart. "
                "Alternatively, launch spicecfg in command line and pass in the correct parameters. "
                "BT5API readers currently do not show the card number in this UI.");
            ImGui::Spacing();

            // show scanned card numbers
            for (int player = 0; player < 2; player++) {
                ImGui::PushID(("CardReaderDisp" + to_string(player)).c_str());
                ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Last card detected for player %i", player + 1);

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
        ImGui::SeparatorText("More tips");
        ImGui::Spacing();
        ImGui::BeginDisabled();
        ImGui::TextWrapped("To debug card reader issues, run spice.exe -cfg in command line and check the log.");
        ImGui::TextWrapped(
            "If you have multiple players, try opening Card Manager window in the game. "
            "Check the key bind in Overlay tab for Toggle Card Manager.");
        ImGui::EndDisabled();
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
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), cat.c_str());
        ImGui::Separator();

        // render table
        // tables must share the same ID to have synced column settings
        if (ImGui::BeginTable("OptionsTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Option", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("CMD Line Parameter", ImGuiTableColumnFlags_WidthFixed, 216);
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 240);

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
                    
                    // check for game exclusivity
                    if (!definition.game_name.empty()) {
                        if (definition.game_name != this->games_selected_name) {
                            continue;
                        }
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
                ImGui::Indent(INDENT);
                ImGui::TableNextRow();

                // option name
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (option.is_active()) {
                    // active option
                    if (option.disabled || definition.disabled) {
                        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.f, 1.f), "%s", definition.title.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "%s", definition.title.c_str());
                    }
                } else if (definition.hidden
                || (!definition.game_name.empty() && definition.game_name != this->games_selected_name)) {
                    // wrong game - grayed out
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "%s", definition.title.c_str());
                } else {
                    // normal text
                    ImGui::Text("%s", definition.title.c_str());
                }
                if (ImGui::IsItemHovered()) {
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
                if (ImGui::IsItemHovered()) {
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
                        if (ImGui::IsItemHovered()) {
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
                        if (ImGui::IsItemHovered()) {
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
                        if (ImGui::IsItemHovered()) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    case OptionType::Text: {
                        char buffer[512];
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
                        if (ImGui::IsItemHovered()) {
                            ImGui::HelpTooltip(definition.desc.c_str());
                        }
                        break;
                    }
                    case OptionType::Enum: {
                        std::string current_item = option.value_text();
                        for (auto &element : definition.elements) {
                            if (element.first == current_item) {
                                current_item += fmt::format(" ({})", element.second);
                            }
                        }
                        if (current_item.empty()) {
                            current_item = "Default";
                        }
                        if (ImGui::BeginCombo("##combo", current_item.c_str(), 0)) {
                            for (auto &element : definition.elements) {
                                bool selected = current_item == element.first;
                                std::string label = element.first;
                                if (!element.second.empty()) {
                                    label += fmt::format(" ({})", element.second);
                                }
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
                        if (ImGui::IsItemHovered()) {
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
                    if (ImGui::Button("Clear")) {
                        this->options_dirty = true;
                        option.value = "";
                        ::Config::getInstance().updateBinding(games_list[games_selected], option);
                    }
                }

                // clean up disabled item flags
                if (option.disabled || definition.disabled) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }

                // disabled help
                if (option.disabled && !definition.disabled) {
                    ImGui::SameLine();
                    ImGui::HelpMarker(
                        "This option can not be edited because it was overriden by command-line options.\n"
                        "Run spicecfg.exe to configure the options and then run spice(64).exe directly.");
                }

                // next item
                ImGui::PopID();
                ImGui::Unindent(INDENT);
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
        ImGui::TextUnformatted(std::string(
            "spice2x (a fork of SpiceTools)\r\n"
            "=========================\r\n" +
            to_string(VERSION_STRING)).c_str());

        ImGui::TextUnformatted("");

        if (ImGui::Button(PROJECT_URL)) {
            launch_shell(PROJECT_URL);
        }

        ImGui::TextUnformatted("");
        ImGui::TextUnformatted(std::string(
                resutil::load_file_string_crlf(IDR_README) +
                "\r\n" +
                "Changelog (Highlights):\r\n" +
                resutil::load_file_string_crlf(IDR_CHANGELOG)).c_str());
    }

    void Config::build_licenses() {
        ImGui::TextUnformatted(resutil::load_file_string_crlf(IDR_LICENSES).c_str());
    }

    void Config::build_launcher() {
        ImGui::TextUnformatted("Please select a game!");
        ImGui::Separator();
        ImGui::BeginChild("Launcher");
        this->build_about();
        ImGui::EndChild();
    }

    void Config::build_page_selector(int *page) {

        // left page selector
        if (ImGui::Button("<")) {
            *page = *page - 1;
            if (*page < 0) {
                *page = 99;
            }
        }

        // page display and reset
        ImGui::SameLine();
        if (ImGui::Button(("Page " + to_string(*page)).c_str())) {
            *page = 0;
        }

        // right page selector
        ImGui::SameLine();
        if (ImGui::Button(">")) {
            *page = *page + 1;
            if (*page > 99) {
                *page = 0;
            }
        }

        // help
        ImGui::SameLine();
        ImGui::HelpMarker("You can bind buttons/lights to multiple inputs/outputs "
                          "at the same time using pages.");
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
        bool licenses_popup = false;
        if (ImGui::BeginMenuBar()) {

            // [spice2x]
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
            if (ImGui::BeginMenu("[spice2x]")) {
                ImGui::PopStyleColor();
                if (ImGui::MenuItem("spice2x.github.io")) {
                    launch_shell(PROJECT_URL);
                }
                if (ImGui::MenuItem("Licenses")) {
                    licenses_popup = true;
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
            ImGui::PushItemWidth(MIN(700, MAX(100, ImGui::GetWindowSize().x - 400)));
            ImGui::Combo("##game_selector", game_selected, games_names.data(), (int)games_list.size());
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(3);

            ImGui::BeginDisabled();
            if (!avs::game::is_model("000")) {
                ImGui::Text("%s", avs::game::get_identifier().c_str());
            }
            ImGui::EndDisabled();

            ImGui::EndMenuBar();
        }

        // workaround for popups triggered by menu, see https://github.com/ocornut/imgui/issues/331
        if (about_popup) {
            ImGui::OpenPopup("About##topbarpopup");
        }
        if (licenses_popup) {
            ImGui::OpenPopup("Licenses##topbarpopup");
        }

        // draw popups
        {
            const ImVec2 popup_size(
                std::min(ImGui::GetIO().DisplaySize.x * 0.9f, 800.f),
                std::min(ImGui::GetIO().DisplaySize.y * 0.9f, 800.f));

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
            ImGui::SetNextWindowSize(popup_size, ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(popup_pos, ImGuiCond_Appearing);
            bool unused_open3 = true;
            if (ImGui::BeginPopupModal("Licenses##topbarpopup", &unused_open3)) {
                this->build_licenses();
                ImGui::EndPopup();
            }
        }
    }
}
