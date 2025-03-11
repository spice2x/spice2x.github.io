#include <winsock2.h>

#include "control.h"

#include <csignal>

#include <psapi.h>

#include "acio/acio.h"
#include "api/controller.h"
#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
#include "build/resource.h"
#include "cfg/analog.h"
#include "cfg/button.h"
#include "external/imgui/imgui_memory_editor.h"
#include "games/io.h"
#include "games/iidx/io.h"
#include "games/shared/lcdhandle.h"
#include "hooks/graphics/graphics.h"
#include "launcher/launcher.h"
#include "launcher/shutdown.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/cpuutils.h"
#include "util/memutils.h"
#include "util/netutils.h"
#include "util/libutils.h"
#include "util/peb.h"
#include "util/resutils.h"
#include "util/utils.h"
#include "touch/touch.h"

#include "acio_status_buffers.h"
#include "eadev.h"
#include "wnd_manager.h"
#include "midi.h"

namespace overlay::windows {

    Control::Control(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "SpiceTools Control";
        this->flags = ImGuiWindowFlags_AlwaysAutoResize;
        this->toggle_button = games::OverlayButtons::ToggleControl;
        this->init_pos = ImVec2(10, 10);
        this->size_min.x = 300;
    }

    Control::~Control() {
    }

    void Control::build_content() {
        top_row_buttons();
        img_gui_view();

        ImGui::Separator();

        avs_info_view();
        acio_view();
        cpu_view();
        graphics_view();
        buttons_view();
        analogs_view();
        lights_view();
        cards_view();
        coin_view();
        control_view();
        api_view();
        raw_input_view();
        touch_view();
        lcd_view();
        about_view();
        ddr_timing_view();
        iidx_effectors_view();
    }

    void Control::top_row_buttons() {

        // memory editor button
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Button("Memory Editor")) {
            this->memory_editor_open = true;
        }

        // memory editor window
        if (this->memory_editor_open) {
            static MemoryEditor memory_editor = MemoryEditor();
            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Once);
            if (ImGui::Begin("Memory Editor", &this->memory_editor_open)) {

                // draw filter
                if (this->memory_editor_filter.Draw("Filter")) {
                    memory_editor_modules.clear();
                    memory_editor_names.clear();
                    memory_editor_selection = -1;
                }

                // obtain modules
                if (memory_editor_modules.empty()) {
                    peb::obtain_modules(&memory_editor_modules);

                    // extract names for combobox
                    for (size_t i = 0; i < memory_editor_modules.size();) {
                        auto s = memory_editor_modules[i].first.c_str();

                        // check if passes filter
                        if (memory_editor_filter.PassFilter(s)) {
                            memory_editor_names.emplace_back(s);
                            i++;
                        } else {
                            memory_editor_modules.erase(memory_editor_modules.begin() + i);
                        }
                    }
                }

                // draw combo box
                ImGui::Combo("DLL Selection",
                        &memory_editor_selection,
                        &memory_editor_names[0],
                        static_cast<int>(memory_editor_names.size()));
                ImGui::Separator();
                if (memory_editor_selection >= 0) {
                    HMODULE module = memory_editor_modules[memory_editor_selection].second;

                    // get module information
                    MODULEINFO module_info{};
                    if (GetModuleInformation(
                            GetCurrentProcess(),
                            module,
                            &module_info,
                            sizeof(MODULEINFO))) {

                        /*
                         * unprotect memory
                         * small hack: don't reset the mode since multiple pages with different modes are affected
                         * they'd get overridden by the original mode of the first page
                         */
                        memutils::VProtectGuard guard(
                                module_info.lpBaseOfDll,
                                module_info.SizeOfImage,
                                PAGE_EXECUTE_READWRITE,
                                false);

                        // draw memory editor
                        memory_editor.DrawContents(
                                module_info.lpBaseOfDll,
                                module_info.SizeOfImage,
                                (size_t) module_info.lpBaseOfDll);
                    } else {
                        ImGui::Text("Could not get module information");
                    }
                } else {
                    ImGui::Text("Please select a module");
                }
            }
            ImGui::End();
        }

        // EA-Dev
        ImGui::SameLine();
        if (ImGui::Button("EA-Dev")) {
            this->children.emplace_back(new EADevWindow(this->overlay));
        }

        // Window Manager
        ImGui::SameLine();
        if (ImGui::Button("Window Manager")) {
            this->children.emplace_back(new WndManagerWindow(this->overlay));
        }
    }

    void Control::img_gui_view() {
        if (ImGui::CollapsingHeader("ImGui")) {

            // display size
            ImGui::Text("Display Size: %dx%d",
                    static_cast<int>(ImGui::GetIO().DisplaySize.x),
                    static_cast<int>(ImGui::GetIO().DisplaySize.y));

            // removed for size (along with setting IMGUI_DISABLE_DEMO_WINDOWS
            // and IMGUI_DISABLE_DEBUG_TOOLS) - saves about 300kb in each
            // binary

            // metrics button
            // this->metrics_open |= ImGui::Button("Metrics Window");
            // if (this->metrics_open) {
            //     ImGui::ShowMetricsWindow(&this->metrics_open);
            // }

            // demo button
            // ImGui::SameLine();
            // if (ImGui::Button("Demo Window")) {
            //     this->demo_open = true;
            // }
            // if (this->demo_open) {
            //     ImGui::ShowDemoWindow(&this->demo_open);
            // }
        }
    }

    void Control::avs_info_view() {
        if (ImGui::CollapsingHeader("AVS")) {

            // game
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Game")) {
                ImGui::BulletText("DLL Name: %s", avs::game::DLL_NAME.c_str());
                ImGui::BulletText("Identifier: %s", avs::game::get_identifier().c_str());
                ImGui::TreePop();
            }

            // core
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Core")) {
                ImGui::BulletText("DLL Name: %s", avs::core::DLL_NAME.c_str());
                ImGui::BulletText("Version: %s", avs::core::VERSION_STR.c_str());
                ImGui::BulletText("%s", fmt::format("Heap Size: {}{}",
                                  (uint64_t) avs::core::HEAP_SIZE,
                                  avs::core::DEFAULT_HEAP_SIZE_SET ? " (Default)" : "").c_str());
                ImGui::BulletText("Log Path: %s", avs::core::LOG_PATH.c_str());
                ImGui::BulletText("Config Path: %s", avs::core::CFG_PATH.c_str());
                ImGui::TreePop();
            }

            // EA3
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("EA3")) {
                ImGui::BulletText("DLL Name: %s", avs::ea3::DLL_NAME.c_str());
                ImGui::BulletText("Version: %s", avs::ea3::VERSION_STR.c_str());
                ImGui::BulletText("Config Path: %s", avs::ea3::CFG_PATH.c_str());
                ImGui::BulletText("App Path: %s", avs::ea3::APP_PATH.c_str());
                ImGui::BulletText("Services: %s", avs::ea3::EA3_BOOT_URL.c_str());
                ImGui::TreePop();
            }
        }
    }

    void Control::acio_view() {
        if (ImGui::CollapsingHeader("ACIO")) {
            ImGui::Columns(4, "acio_columns");
            ImGui::Separator();
            ImGui::Text("Name");
            ImGui::NextColumn();
            ImGui::Text("Hook");
            ImGui::NextColumn();
            ImGui::Text("Attached");
            ImGui::NextColumn();
            ImGui::NextColumn();
            ImGui::Separator();
            for (auto &module : acio::MODULES) {
                ImGui::PushID((void *) module);
                ImGui::Text("%s", module->name.c_str());
                ImGui::NextColumn();
                ImGui::Text("%s", acio::hook_mode_str(module->hook_mode));
                ImGui::NextColumn();
                ImGui::Text("%s", module->attached ? "true" : "false");
                ImGui::NextColumn();
                if (module->status_buffer && module->status_buffer_size) {
                    if (ImGui::Button("Status")) {
                        this->children.emplace_back(new ACIOStatusBuffers(overlay, module));
                    }
                }
                ImGui::NextColumn();
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::Columns(1);
        }
    }

    void Control::cpu_view() {
        auto cpu_load_values = cpuutils::get_load();
        if (cpu_load_values.size() && ImGui::CollapsingHeader("CPU")) {

            // print detected cores
            ImGui::BulletText("Detected cores: %i", (int) std::thread::hardware_concurrency());

            // make sure the temporary buffer has enough space
            while (this->cpu_values.size() < cpu_load_values.size()) {
                this->cpu_values.emplace_back(0.f);
            }

            // iterate cores
            for (size_t cpu = 0; cpu < cpu_load_values.size(); cpu++) {

                // update average
                auto avg_load = MIN(MAX(this->cpu_values[cpu] +
                        (cpu_load_values[cpu] - this->cpu_values[cpu]) * ImGui::GetIO().DeltaTime, 0), 100);
                this->cpu_values[cpu] = avg_load;

                // draw content
                ImGui::BulletText("CPU #%i:", (int) cpu + 1);
                ImGui::SameLine();
                ImGui::ProgressBar(avg_load * 0.01f, ImVec2(64, 0));
            }
        }
    }

    void Control::graphics_view() {
        if (ImGui::CollapsingHeader("Graphics")) {

            // screenshot button
            if (ImGui::Button("Take Screenshot")) {
                graphics_screenshot_trigger();
            }

            // graphics information
            ImGui::BulletText("D3D9 Adapter ID: %lu",
                    overlay->adapter_identifier.DeviceId);
            ImGui::BulletText("D3D9 Adapter Name: %s",
                    overlay->adapter_identifier.DeviceName);
            ImGui::BulletText("D3D9 Adapter Revision: %lu",
                    overlay->adapter_identifier.Revision);
            ImGui::BulletText("D3D9 Adapter SubSys ID: %lu",
                    overlay->adapter_identifier.SubSysId);
            ImGui::BulletText("D3D9 Adapter Vendor ID: %lu",
                    overlay->adapter_identifier.VendorId);
            ImGui::BulletText("D3D9 Adapter WQHL Level: %lu",
                    overlay->adapter_identifier.WHQLLevel);
            ImGui::BulletText("D3D9 Adapter Driver: %s",
                    overlay->adapter_identifier.Driver);
            ImGui::BulletText("%s", fmt::format("D3D9 Adapter Driver Version: {}",
                    overlay->adapter_identifier.DriverVersion.QuadPart).c_str());
            ImGui::BulletText("D3D9 Adapter Description: %s",
                    overlay->adapter_identifier.Description);
            ImGui::BulletText("D3D9 Adapter GUID: %s",
                    guid2s(overlay->adapter_identifier.DeviceIdentifier).c_str());
        }
    }

    void Control::buttons_view() {
        auto buttons = games::get_buttons(eamuse_get_game());
        if (buttons && !buttons->empty() && ImGui::CollapsingHeader("Buttons")) {

            // print each button state
            for (auto &button : *buttons) {

                // state
                float state = GameAPI::Buttons::getVelocity(RI_MGR, button);
                ImGui::ProgressBar(state, ImVec2(32.f, 0));

                // mouse down handler
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsAnyMouseDown()) {
                        button.override_state = GameAPI::Buttons::BUTTON_PRESSED;
                        button.override_velocity = 1.f;
                        button.override_enabled = true;
                    } else {
                        button.override_enabled = false;
                    }
                }

                // mark overridden items
                if (button.override_enabled) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));
                }

                // text
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (RI_MGR && button.isSet()) {
                    ImGui::Text("%s [%s]",
                                button.getName().c_str(),
                                button.getDisplayString(RI_MGR.get()).c_str());
                } else {
                    ImGui::Text("%s", button.getName().c_str());
                }

                // pop override color
                if (button.override_enabled) {
                    ImGui::PopStyleColor();
                }
            }
        }
    }

    void Control::analogs_view() {
        auto analogs = games::get_analogs(eamuse_get_game());
        if (analogs && !analogs->empty() && ImGui::CollapsingHeader("Analogs")) {

            // print each button state
            for (auto &analog : *analogs) {

                // state
                float state = GameAPI::Analogs::getState(RI_MGR, analog);
                ImGui::ProgressBar(state, ImVec2(32.f, 0));

                // mouse down handler
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsAnyMouseDown()) {
                        analog.override_state = 1.f;
                        analog.override_enabled = true;
                    } else {
                        analog.override_enabled = false;
                    }
                }

                // mark overridden items
                if (analog.override_enabled) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));
                }

                // text
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (RI_MGR && analog.isSet()) {
                    ImGui::Text("%s [%s]",
                                analog.getName().c_str(),
                                analog.getDisplayString(RI_MGR.get()).c_str());
                } else {
                    ImGui::Text("%s", analog.getName().c_str());
                }

                // pop override color
                if (analog.override_enabled) {
                    ImGui::PopStyleColor();
                }
            }
        }
    }

    void Control::lights_view() {
        auto lights = games::get_lights(eamuse_get_game());
        if (lights && !lights->empty() && ImGui::CollapsingHeader("Lights")) {

            // print each button state
            for (auto &light : *lights) {

                // state
                float state = GameAPI::Lights::readLight(RI_MGR, light);
                ImGui::ProgressBar(state, ImVec2(32.f, 0));

                // mouse down handler
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsAnyMouseDown()) {
                        light.override_state = 1.f;
                        light.override_enabled = true;
                    } else {
                        light.override_enabled = false;
                    }
                    GameAPI::Lights::writeLight(RI_MGR, light, light.last_state);
                    RI_MGR->devices_flush_output();
                }

                // mark overridden items
                if (light.override_enabled) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));
                }

                // text
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (RI_MGR && light.isSet()) {
                    ImGui::Text("%s [%s]",
                                light.getName().c_str(),
                                light.getDisplayString(RI_MGR.get()).c_str());
                } else {
                    ImGui::Text("%s", light.getName().c_str());
                }

                // pop override color
                if (light.override_enabled) {
                    ImGui::PopStyleColor();
                }
            }
        }
    }

    void Control::cards_view() {
        if (ImGui::CollapsingHeader("Cards")) {
            ImGui::InputTextWithHint("Card ID", "E0040123456789AB",
                    this->card_input,
                    std::size(this->card_input),
                    ImGuiInputTextFlags_CharsHexadecimal
                    | ImGuiInputTextFlags_CharsUppercase);
            if (strlen(this->card_input) < 16) {
                ImGui::Text("Please enter your card identifier...");
            } else {
                if (ImGui::Button("Insert P1")) {
                    uint8_t card_data[8];
                    if (hex2bin(this->card_input, card_data)) {
                        eamuse_card_insert(0, card_data);
                    }
                }
                if (eamuse_get_game_keypads() > 1) {
                    ImGui::SameLine();
                    if (ImGui::Button("Insert P2")) {
                        uint8_t card_data[8];
                        if (hex2bin(this->card_input, card_data)) {
                            eamuse_card_insert(1, card_data);
                        }
                    }
                }
            }
        }
    }

    void Control::coin_view() {
        if (ImGui::CollapsingHeader("Coins")) {
            auto coinstock = eamuse_coin_get_stock();
            ImGui::Text("Blocker: %s", eamuse_coin_get_block() ? "closed" : "open");
            ImGui::Text("Coinstock: %i", coinstock);
            ImGui::Separator();
            if (ImGui::Button("Add Coin")) {
                eamuse_coin_add();
            }
            if (coinstock != 0) {
                ImGui::SameLine();
                if (ImGui::Button("Consume")) {
                    eamuse_coin_consume_stock();
                }
            }
        }
    }

    void Control::control_view() {
        if (ImGui::CollapsingHeader("Control")) {

            // launcher utils
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Launcher Utils")) {
                if (ImGui::Button("Restart")) {
                    launcher::restart();
                }
                if (ImGui::Button("Terminate")) {
                    launcher::shutdown(0);
                }
                ImGui::TreePop();
            }

            // signal triggers
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Signal Triggers")) {
                if (ImGui::Button("Raise SIGABRT")) {
                    ::raise(SIGABRT);
                }
                if (ImGui::Button("Raise SIGFPE")) {
                    ::raise(SIGFPE);
                }
                if (ImGui::Button("Raise SIGILL")) {
                    ::raise(SIGILL);
                }
                if (ImGui::Button("Raise SIGINT")) {
                    ::raise(SIGINT);
                }
                if (ImGui::Button("Raise SIGSEGV")) {
                    ::raise(SIGSEGV);
                }
                if (ImGui::Button("Raise SIGTERM")) {
                    ::raise(SIGTERM);
                }
                ImGui::TreePop();
            }
        }
    }

    void Control::api_view() {
        if (API_CONTROLLER != nullptr && ImGui::CollapsingHeader("API")) {
            std::vector<api::ClientState> client_states;
            API_CONTROLLER->obtain_client_states(&client_states);

            // show ip addresses
            auto ip_addresses = netutils::get_local_addresses();
            if (!ip_addresses.empty()) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Local IP-Addresses")) {
                    for (auto &adr : ip_addresses) {
                        ImGui::BulletText("%s", adr.c_str());
                    }
                    ImGui::TreePop();
                    ImGui::Separator();
                }
            }

            // client count
            ImGui::Text("Connected clients: %u", (unsigned int) client_states.size());

            // iterate clients
            for (auto &client : client_states) {
                auto address = API_CONTROLLER->get_ip_address(client.address);
                if (ImGui::TreeNode(("Client @ " + address).c_str())) {
                    if (client.password.empty()) {
                        ImGui::Text("No password set.");
                    } else {
                        ImGui::Text("Password set.");
                    }
                    if (ImGui::TreeNode("Modules")) {
                        for (auto &module : client.modules) {
                            if (ImGui::TreeNode(module->name.c_str())) {
                                ImGui::Text("Password force: %i", module->password_force);
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
            }
        }
    }

    void Control::raw_input_view() {
        if (RI_MGR != nullptr && ImGui::CollapsingHeader("RawInput")) {

            // midi control
            if (ImGui::Button("MIDI-Control")) {
                this->children.push_back(new MIDIWindow(this->overlay));
            }

            // device count
            auto devices = RI_MGR->devices_get();
            ImGui::Text("Devices detected: %u", (unsigned int) devices.size());

            // iterate devices
            for (auto &device : devices) {
                if (ImGui::TreeNode(("#" + to_string(device.id) + ": " + device.desc).c_str())) {
                    ImGui::Text("GUID: %s", device.info.guid_str.c_str());
                    ImGui::Text("Output: %i", device.output_enabled);
                    if (device.input_hz > 0 || device.input_hz_max > 0) {
                        ImGui::Text("Input rate (cur): %.2fHz", device.input_hz);
                        ImGui::Text("Input rate (max): %.2fHz", device.input_hz_max);
                    }
                    switch (device.type) {
                        case rawinput::MOUSE: {
                            auto mouse = device.mouseInfo;
                            ImGui::Text("Type: Mouse");
                            ImGui::Text("X: %ld", mouse->pos_x);
                            ImGui::Text("Y: %ld", mouse->pos_y);
                            ImGui::Text("Wheel: %ld", mouse->pos_wheel);

                            // keys
                            std::stringstream keys;
                            keys << "[";
                            for (auto key : mouse->key_states) {
                                keys << (key ? "1," : "0,");
                            }
                            keys << "]";
                            ImGui::Text("Keys: %s", keys.str().c_str());

                            break;
                        }
                        case rawinput::KEYBOARD: {
                            auto keyboard = device.keyboardInfo;
                            ImGui::Text("Type: Keyboard");

                            // keys
                            std::stringstream keys;
                            keys << "[";
                            for (size_t i = 0; i < std::size(keyboard->key_states); i++) {
                                if (keyboard->key_states[i]) {
                                    keys << i << ",";
                                }
                            }
                            keys << "]";
                            ImGui::Text("Keys: %s", keys.str().c_str());

                            break;
                        }
                        case rawinput::HID: {
                            auto hid = device.hidInfo;
                            ImGui::Text("Type: HID");
                            ImGui::Text("VID: %04X", hid->attributes.VendorID);
                            ImGui::Text("PID: %04X", hid->attributes.ProductID);
                            ImGui::Text("VER: %i", hid->attributes.VersionNumber);
                            switch (hid->driver) {
                                case rawinput::HIDDriver::PacDrive:
                                    ImGui::Text("Driver: PacDrive");
                                    break;
                                default:
                                    ImGui::Text("Driver: Default");
                                    break;
                            }

                            // button states
                            if (!hid->button_states.empty() && ImGui::TreeNode("Button States")) {
                                size_t button_cap_name = 0;
                                for (auto state_list : hid->button_states) {
                                    for (auto state : state_list) {
                                        ImGui::Text("%s: %i",
                                                hid->button_caps_names[button_cap_name++].c_str(),
                                                state ? 1 : 0);
                                    }
                                }
                                ImGui::TreePop();
                            }

                            // button output states
                            if (!hid->button_output_states.empty() && ImGui::TreeNode("Button Output States")) {
                                size_t button_output_cap_name = 0;
                                for (auto state_list : hid->button_output_states) {
                                    for (auto state : state_list) {
                                        ImGui::Text("%s: %i",
                                                hid->button_output_caps_names[button_output_cap_name++].c_str(),
                                                state ? 1 : 0);
                                    }
                                }
                                ImGui::TreePop();
                            }

                            // analog states
                            if (!hid->value_states.empty() && ImGui::TreeNode("Analog States")) {
                                size_t value_cap_name = 0;
                                for (auto analog_state : hid->value_states) {
                                    ImGui::Text("%s: %.2f",
                                                hid->value_caps_names[value_cap_name++].c_str(),
                                                analog_state);
                                }
                                ImGui::TreePop();
                            }

                            // analog output states
                            if (!hid->value_output_states.empty() && ImGui::TreeNode("Analog Output States")) {
                                size_t value_output_cap_name = 0;
                                for (auto analog_state : hid->value_output_states) {
                                    ImGui::Text("%s: %.2f",
                                                hid->value_output_caps_names[value_output_cap_name++].c_str(),
                                                analog_state);
                                }
                                ImGui::TreePop();
                            }

                            break;
                        }
                        case rawinput::MIDI: {
                            ImGui::Text("Type: MIDI");
                            break;
                        }
                        case rawinput::SEXTET_OUTPUT: {
                            ImGui::Text("Type: Sextet");
                            break;
                        }
                        case rawinput::PIUIO_DEVICE: {
                            ImGui::Text("Type: PIUIO");
                            break;
                        }
                        case rawinput::DESTROYED: {
                            ImGui::Text("Disconnected.");
                            break;
                        }
                        case rawinput::UNKNOWN:
                        default:
                            ImGui::Text("Type: Unknown");
                    }
                    ImGui::TreePop();
                }
            }
        }
    }

    void Control::touch_view() {
        if (ImGui::CollapsingHeader("Touch")) {

            // status
            ImGui::Text("Status: %s", is_touch_available() ? "available" : "unavailable");

            // touch points
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Touch Points")) {

                // get touch points
                std::vector<TouchPoint> touch_points;
                touch_get_points(touch_points);
                for (auto &tp : touch_points) {

                    // draw touch point
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    if (ImGui::TreeNode((void *) (size_t) tp.id, "TP #%lu", tp.id)) {
                        ImGui::Text("X: %ld", tp.x);
                        ImGui::Text("Y: %ld", tp.y);
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }
    }

    void Control::lcd_view() {
        if (games::shared::LCD_ENABLED && ImGui::CollapsingHeader("LCD")) {
            ImGui::Text("Enabled: %s", games::shared::LCD_ENABLED ? "true" : "false");
            ImGui::Text("CSM: %s", games::shared::LCD_CSM.c_str());
            ImGui::Text("BRI: %i", games::shared::LCD_BRI);
            ImGui::Text("CON: %i", games::shared::LCD_CON);
            ImGui::Text("RED: %i", games::shared::LCD_RED);
            ImGui::Text("GREEN: %i", games::shared::LCD_GREEN);
            ImGui::Text("BLUE: %i", games::shared::LCD_BLUE);
            ImGui::Text("BL: %i", games::shared::LCD_BL);
        }
    }

    void Control::about_view() {
        if (ImGui::CollapsingHeader("About")) {
            if (ImGui::TreeNode("Changelog")) {
                ImGui::Separator();
                if (ImGui::BeginChild("changelog", ImVec2(400, 400))) {
                    ImGui::TextUnformatted(resutil::load_file_string(IDR_CHANGELOG).c_str());
                }
                ImGui::EndChild();
            }
            if (ImGui::TreeNode("Licenses")) {
                ImGui::Separator();
                if (ImGui::BeginChild("changelog", ImVec2(400, 400), false,
                        ImGuiWindowFlags_HorizontalScrollbar
                        | ImGuiWindowFlags_AlwaysHorizontalScrollbar)) {
                    ImGui::TextUnformatted(resutil::load_file_string(IDR_LICENSES).c_str());
                }
                ImGui::EndChild();
            }
        }
    }

    void Control::ddr_timing_view() {
        if (avs::game::is_model("MDX") && ImGui::CollapsingHeader("DDR Timing")) {

            // patches
            struct ddr_patch {
                const char *ext;
                const char *name;
                const char *format;
                int min;
                int max;
                size_t offset;
                intptr_t offset_ptr = 0;
            };

            static struct ddr_patch PATCHES[] = {

                    // patches for MDX-001-2019042200
                    { "2019042200", "Sound Offset", "%d ms", 0, 1000, 0x1CCC5 },
                    { "2019042200", "Render Offset", "%d ms", 0, 1000, 0x1CD0A },
                    { "2019042200", "Input Offset", "%d ms", 0, 1000, 0x1CCE5 },
                    { "2019042200", "Bomb Offset", "%d frames", 0, 10, 0x1CCC0 },
                    { "2019042200", "SSQ Offset", "%d ms", -1000, 1000, 0x1CCCA },
                    { "2019042200", "Cabinet Type", "%d", 0, 6, 0x1CDAE },
            };

            // check if patches available
            bool patches_available = false;
            for (auto &patch : PATCHES) {
                if (avs::game::is_ext(patch.ext)) {
                    patches_available = true;
                    break;
                }
            }

            // show message if no patches available
            if (!patches_available) {
                ImGui::Text("No offsets known for this version.");
            } else {

                // iterate patches
                for (auto &patch : PATCHES) {
                    if (avs::game::is_ext(patch.ext)) {

                        // check if pointer is uninitialized
                        if (patch.offset_ptr == 0) {

                            // get module information
                            auto dll_path = MODULE_PATH / "gamemdx.dll";

                            // get dll_module
                            auto dll_module = libutils::try_module(dll_path);
                            if (!dll_module) {
                                // no fatal error, might just not be loaded yet
                                break;
                            }

                            // get module information
                            MODULEINFO dll_module_info {};
                            if (GetModuleInformation(
                                    GetCurrentProcess(),
                                    dll_module,
                                    &dll_module_info,
                                    sizeof(MODULEINFO)))
                            {
                                // convert offset to RVA
                                auto rva = libutils::offset2rva(dll_path, patch.offset);
                                if (rva && rva != ~0) {

                                    // get data pointer
                                    patch.offset_ptr = reinterpret_cast<intptr_t>(dll_module_info.lpBaseOfDll) + rva;
                                } else {

                                    // invalidate
                                    patch.offset_ptr = -1;
                                }

                            } else {

                                // invalidate
                                patch.offset_ptr = -1;
                            }
                        }

                        // check if pointer is valid
                        if (patch.offset_ptr != -1) {
                            auto *value_ptr = reinterpret_cast<uint16_t *>(patch.offset_ptr);

                            // draw drag widget
                            int value = *value_ptr;
                            ImGui::DragInt(patch.name, &value, 0.2f, patch.min, patch.max, patch.format);

                            // write value back
                            if (value != *value_ptr) {
                                memutils::VProtectGuard guard(value_ptr, sizeof(uint16_t));
                                *value_ptr = value;
                            }
                        }
                    }
                }
            }
        }
    }

    void Control::iidx_effectors_view() {
        if (avs::game::is_model("LDJ") && ImGui::CollapsingHeader("IIDX Effectors")) {

            // effector analog entries
            static const std::map<size_t, const char *> ANALOG_ENTRIES {
                    { games::iidx::Analogs::VEFX, "VEFX" },
                    { games::iidx::Analogs::LowEQ, "LoEQ" },
                    { games::iidx::Analogs::HiEQ, "HiEQ" },
                    { games::iidx::Analogs::Filter, "Flt" },
                    { games::iidx::Analogs::PlayVolume, "Vol" },
            };

            // iterate analogs
            float hue = 0.f;
            bool overridden = false;
            static auto analogs = games::get_analogs(eamuse_get_game());
            for (auto &[index, name] : ANALOG_ENTRIES) {

                // safety check
                if (index >= analogs->size()) {
                    continue;
                }

                // get analog
                auto &analog = (*analogs)[index];
                overridden |= analog.override_enabled;

                // push id and style
                ImGui::PushID((void *) name);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4) ImColor::HSV(hue, 0.5f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, (ImVec4) ImColor::HSV(hue, 0.6f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, (ImVec4) ImColor::HSV(hue, 0.7f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, (ImVec4) ImColor::HSV(hue, 0.9f, 0.9f));
                if (analog.override_enabled) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.8f, 1.f));
                }

                // vertical slider
                auto new_state = analog.override_enabled ? analog.override_state
                        : GameAPI::Analogs::getState(RI_MGR, analog);
                if (hue > 0.f) {
                    ImGui::SameLine();
                }
                ImGui::VSliderFloat("##v", ImVec2(32, 160), &new_state, 0.f, 1.f, name);
                if (new_state != analog.override_state) {
                    analog.override_state = new_state;
                    analog.override_enabled = true;
                }

                // pop id and style
                ImGui::PopStyleColor(5);
                ImGui::PopID();

                // rainbow
                hue += 1.f / 7;
            }

            // reset button
            if (overridden) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                if (ImGui::Button("Reset")) {
                    for (auto &[index, name] : ANALOG_ENTRIES) {
                        if (index < analogs->size()) {
                            (*analogs)[index].override_enabled = false;
                        }
                    }
                }
                ImGui::PopStyleColor();
            }
        }
    }

}
