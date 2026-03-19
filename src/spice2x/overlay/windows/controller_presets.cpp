#include "controller_presets.h"
#include "controller_presets_builtin.h"

#include <algorithm>
#include <filesystem>

#include "external/tinyxml2/tinyxml2.h"
#include "cfg/button.h"
#include "util/logging.h"

namespace overlay::windows {

    // helpers - iterate both buttons and keypad_buttons
    static void count_buttons_in(const std::vector<TemplateButtonBinding> &btns, int &naive, int &device) {
        for (auto &btn : btns) {
            if (btn.primary.is_naive()) naive++;
            if (btn.primary.is_device()) device++;
            for (auto &alt : btn.alternatives) {
                if (alt.is_naive()) naive++;
                if (alt.is_device()) device++;
            }
        }
    }

    int ControllerTemplate::count_naive_buttons() const {
        int naive = 0, device = 0;
        count_buttons_in(buttons, naive, device);
        count_buttons_in(keypad_buttons, naive, device);
        return naive;
    }

    int ControllerTemplate::count_device_buttons() const {
        int naive = 0, device = 0;
        count_buttons_in(buttons, naive, device);
        count_buttons_in(keypad_buttons, naive, device);
        return device;
    }

    int ControllerTemplate::count_device_analogs() const {
        int count = 0;
        for (auto &a : analogs) {
            if (a.is_device()) count++;
        }
        return count;
    }

    int ControllerTemplate::count_device_lights() const {
        int count = 0;
        for (auto &l : lights) {
            if (l.primary.is_device()) count++;
            for (auto &alt : l.alternatives) {
                if (alt.is_device()) count++;
            }
        }
        return count;
    }

    static void collect_devices_from(const std::vector<TemplateButtonBinding> &btns,
                                     std::set<std::string> &devices) {
        for (auto &btn : btns) {
            if (btn.primary.is_device()) devices.insert(btn.primary.device_identifier);
            for (auto &alt : btn.alternatives) {
                if (alt.is_device()) devices.insert(alt.device_identifier);
            }
        }
    }

    std::set<std::string> ControllerTemplate::get_used_devices() const {
        std::set<std::string> devices;
        collect_devices_from(buttons, devices);
        collect_devices_from(keypad_buttons, devices);
        for (auto &a : analogs) {
            if (a.is_device()) devices.insert(a.device_identifier);
        }
        for (auto &l : lights) {
            if (l.primary.is_device()) devices.insert(l.primary.device_identifier);
            for (auto &alt : l.alternatives) {
                if (alt.is_device()) devices.insert(alt.device_identifier);
            }
        }
        return devices;
    }

    static void collect_sources_from(const std::vector<TemplateButtonBinding> &btns,
                                     std::set<std::string> &sources_set, bool &has_naive) {
        for (auto &btn : btns) {
            if (btn.primary.is_naive()) has_naive = true;
            if (btn.primary.is_device()) sources_set.insert(btn.primary.device_identifier);
            for (auto &alt : btn.alternatives) {
                if (alt.is_naive()) has_naive = true;
                if (alt.is_device()) sources_set.insert(alt.device_identifier);
            }
        }
    }

    std::vector<std::string> ControllerTemplate::get_binding_sources() const {
        std::set<std::string> sources_set;
        bool has_naive = false;

        collect_sources_from(buttons, sources_set, has_naive);
        collect_sources_from(keypad_buttons, sources_set, has_naive);
        for (auto &a : analogs) {
            if (a.is_device()) sources_set.insert(a.device_identifier);
        }
        for (auto &l : lights) {
            if (l.primary.is_device()) sources_set.insert(l.primary.device_identifier);
            for (auto &alt : l.alternatives) {
                if (alt.is_device()) sources_set.insert(alt.device_identifier);
            }
        }

        std::vector<std::string> result;
        if (has_naive) {
            result.emplace_back("Naive");
        }
        for (auto &s : sources_set) {
            result.push_back(s);
        }

        // sort alphabetically
        std::sort(result.begin(), result.end());

        return result;
    }

    void ControllerTemplate::rename_source(const std::string &old_id, const std::string &new_id) {
        auto rename_in_buttons = [&](std::vector<TemplateButtonBinding> &btns) {
            for (auto &btn : btns) {
                if (btn.primary.device_identifier == old_id) btn.primary.device_identifier = new_id;
                for (auto &alt : btn.alternatives) {
                    if (alt.device_identifier == old_id) alt.device_identifier = new_id;
                }
            }
        };
        rename_in_buttons(buttons);
        rename_in_buttons(keypad_buttons);
        for (auto &a : analogs) {
            if (a.device_identifier == old_id) a.device_identifier = new_id;
        }
        for (auto &l : lights) {
            if (l.primary.device_identifier == old_id) l.primary.device_identifier = new_id;
            for (auto &alt : l.alternatives) {
                if (alt.device_identifier == old_id) alt.device_identifier = new_id;
            }
        }
    }

    std::string ControllerTemplate::get_source_summary(const std::string &source) const {
        bool is_naive = (source == "Naive");
        std::string result;

        // collect button lines from a button vector
        auto collect_btn_lines = [&](const std::vector<TemplateButtonBinding> &btns,
                                     std::vector<std::string> &lines) {
            for (auto &tb : btns) {
                auto check_entry = [&](const TemplateButtonEntry &e) {
                    if (is_naive && e.is_naive()) {
                        Button b("");
                        b.setVKey(e.vKey);
                        lines.push_back(tb.name + " = " + (e.vKey == INVALID_VKEY ? "None" : b.getVKeyString()));
                    } else if (!is_naive && e.is_device() && e.device_identifier == source) {
                        std::string desc = std::to_string(e.vKey);
                        if (e.analog_type != BAT_NONE) {
                            desc += " (analog:" + std::to_string((int)e.analog_type) + ")";
                        }
                        lines.push_back(tb.name + " = " + desc);
                    }
                };
                check_entry(tb.primary);
                for (auto &alt : tb.alternatives) {
                    check_entry(alt);
                }
            }
        };

        // buttons
        std::vector<std::string> btn_lines;
        collect_btn_lines(buttons, btn_lines);
        if (!btn_lines.empty()) {
            result += "Buttons:\n";
            for (auto &line : btn_lines) {
                result += "  " + line + "\n";
            }
        }

        // keypad buttons
        std::vector<std::string> kp_lines;
        collect_btn_lines(keypad_buttons, kp_lines);
        if (!kp_lines.empty()) {
            result += "Keypad:\n";
            for (auto &line : kp_lines) {
                result += "  " + line + "\n";
            }
        }

        // analogs
        if (!is_naive) {
            std::vector<std::string> analog_lines;
            for (auto &a : analogs) {
                if (a.is_device() && a.device_identifier == source) {
                    analog_lines.push_back(a.name + " (idx:" + std::to_string(a.index) + ")");
                }
            }
            if (!analog_lines.empty()) {
                result += "Analogs:\n";
                for (auto &line : analog_lines) {
                    result += "  " + line + "\n";
                }
            }
        }

        // lights
        if (!is_naive) {
            std::vector<std::string> light_lines;
            for (auto &l : lights) {
                auto check_light = [&](const TemplateLightEntry &e) {
                    if (e.is_device() && e.device_identifier == source) {
                        light_lines.push_back(l.name + " (idx:" + std::to_string(e.index) + ")");
                    }
                };
                check_light(l.primary);
                for (auto &alt : l.alternatives) {
                    check_light(alt);
                }
            }
            if (!light_lines.empty()) {
                result += "Lights:\n";
                for (auto &line : light_lines) {
                    result += "  " + line + "\n";
                }
            }
        }

        if (result.empty()) {
            result = "(no bindings)";
        }
        // trim trailing newline
        while (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        return result;
    }

    // file path
    static std::filesystem::path get_templates_path() {
        return std::filesystem::path(_wgetenv(L"APPDATA")) / L"spicetools_presets.xml";
    }

    // XML persistence
    static void write_button_entry(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent,
                                   const std::string &name, const TemplateButtonEntry &entry) {
        auto *el = doc.NewElement("button");
        el->SetAttribute("name", name.c_str());
        el->SetAttribute("vkey", entry.vKey);
        el->SetAttribute("analogtype", (int)entry.analog_type);
        el->SetAttribute("devid", entry.device_identifier.c_str());
        el->SetAttribute("invert", entry.invert);
        el->SetAttribute("debounce_up", entry.debounce_up);
        el->SetAttribute("debounce_down", entry.debounce_down);
        el->SetAttribute("velocity_threshold", entry.velocity_threshold);
        parent->InsertEndChild(el);
    }

    static TemplateButtonEntry read_button_entry(tinyxml2::XMLElement *el) {
        TemplateButtonEntry entry;
        int vkey = INVALID_VKEY;
        el->QueryIntAttribute("vkey", &vkey);
        entry.vKey = (unsigned short)vkey;

        int atype = 0;
        el->QueryIntAttribute("analogtype", &atype);
        entry.analog_type = (ButtonAnalogType)atype;

        const char *devid = el->Attribute("devid");
        entry.device_identifier = devid ? devid : "";

        el->QueryBoolAttribute("invert", &entry.invert);
        el->QueryDoubleAttribute("debounce_up", &entry.debounce_up);
        el->QueryDoubleAttribute("debounce_down", &entry.debounce_down);

        int vel = 0;
        el->QueryIntAttribute("velocity_threshold", &vel);
        entry.velocity_threshold = (unsigned short)vel;

        return entry;
    }

    static void write_analog(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent,
                             const TemplateAnalogBinding &analog) {
        auto *el = doc.NewElement("analog");
        el->SetAttribute("name", analog.name.c_str());
        el->SetAttribute("devid", analog.device_identifier.c_str());
        el->SetAttribute("index", analog.index);
        el->SetAttribute("sensivity", analog.sensitivity);  // keep typo for compat
        el->SetAttribute("deadzone", analog.deadzone);
        el->SetAttribute("deadzone_mirror", analog.deadzone_mirror);
        el->SetAttribute("invert", analog.invert);
        el->SetAttribute("smoothing", analog.smoothing);
        el->SetAttribute("multiplier", analog.multiplier);
        el->SetAttribute("relative", analog.relative_mode);
        el->SetAttribute("delay", analog.delay_buffer_depth);
        parent->InsertEndChild(el);
    }

    static TemplateAnalogBinding read_analog(tinyxml2::XMLElement *el) {
        TemplateAnalogBinding a;
        const char *name = el->Attribute("name");
        a.name = name ? name : "";

        const char *devid = el->Attribute("devid");
        a.device_identifier = devid ? devid : "";

        int idx = USHRT_MAX;
        el->QueryIntAttribute("index", &idx);
        a.index = (unsigned short)idx;

        el->QueryFloatAttribute("sensivity", &a.sensitivity);  // keep typo
        el->QueryFloatAttribute("deadzone", &a.deadzone);
        el->QueryBoolAttribute("deadzone_mirror", &a.deadzone_mirror);
        el->QueryBoolAttribute("invert", &a.invert);
        el->QueryBoolAttribute("smoothing", &a.smoothing);
        el->QueryIntAttribute("multiplier", &a.multiplier);
        el->QueryBoolAttribute("relative", &a.relative_mode);
        el->QueryIntAttribute("delay", &a.delay_buffer_depth);

        return a;
    }

    static void write_light_entry(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent,
                                  const std::string &name, const TemplateLightEntry &entry) {
        auto *el = doc.NewElement("light");
        el->SetAttribute("name", name.c_str());
        el->SetAttribute("devid", entry.device_identifier.c_str());
        el->SetAttribute("index", entry.index);
        parent->InsertEndChild(el);
    }

    static TemplateLightEntry read_light_entry(tinyxml2::XMLElement *el) {
        TemplateLightEntry entry;
        const char *devid = el->Attribute("devid");
        entry.device_identifier = devid ? devid : "";

        int idx = 0;
        el->QueryIntAttribute("index", &idx);
        entry.index = (unsigned int)idx;

        return entry;
    }

    // load all user templates from XML
    std::vector<ControllerTemplate> load_user_templates() {
        std::vector<ControllerTemplate> templates;
        auto path = get_templates_path();

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
            return templates;
        }

        auto *root = doc.RootElement();
        if (!root) return templates;

        auto *tmpl_el = root->FirstChildElement("template");
        while (tmpl_el) {
            ControllerTemplate tmpl;
            const char *name = tmpl_el->Attribute("name");
            const char *game = tmpl_el->Attribute("game");
            tmpl.name = name ? name : "";
            tmpl.game_name = game ? game : "";
            tmpl.is_builtin = false;

            // buttons
            auto *buttons_el = tmpl_el->FirstChildElement("buttons");
            if (buttons_el) {
                auto *btn_el = buttons_el->FirstChildElement("button");
                while (btn_el) {
                    const char *btn_name = btn_el->Attribute("name");
                    std::string btn_name_str = btn_name ? btn_name : "";

                    // find or create binding
                    TemplateButtonBinding *binding = nullptr;
                    for (auto &b : tmpl.buttons) {
                        if (b.name == btn_name_str) {
                            binding = &b;
                            break;
                        }
                    }

                    if (!binding) {
                        tmpl.buttons.push_back({btn_name_str, read_button_entry(btn_el), {}});
                    } else {
                        // subsequent entries with same name are alternatives
                        binding->alternatives.push_back(read_button_entry(btn_el));
                    }

                    btn_el = btn_el->NextSiblingElement("button");
                }
            }

            // keypad buttons
            auto *kp_el = tmpl_el->FirstChildElement("keypad_buttons");
            if (kp_el) {
                auto *btn_el = kp_el->FirstChildElement("button");
                while (btn_el) {
                    const char *btn_name = btn_el->Attribute("name");
                    std::string btn_name_str = btn_name ? btn_name : "";

                    TemplateButtonBinding *binding = nullptr;
                    for (auto &b : tmpl.keypad_buttons) {
                        if (b.name == btn_name_str) {
                            binding = &b;
                            break;
                        }
                    }

                    if (!binding) {
                        tmpl.keypad_buttons.push_back({btn_name_str, read_button_entry(btn_el), {}});
                    } else {
                        binding->alternatives.push_back(read_button_entry(btn_el));
                    }

                    btn_el = btn_el->NextSiblingElement("button");
                }
            }

            // analogs
            auto *analogs_el = tmpl_el->FirstChildElement("analogs");
            if (analogs_el) {
                auto *analog_el = analogs_el->FirstChildElement("analog");
                while (analog_el) {
                    tmpl.analogs.push_back(read_analog(analog_el));
                    analog_el = analog_el->NextSiblingElement("analog");
                }
            }

            // lights
            auto *lights_el = tmpl_el->FirstChildElement("lights");
            if (lights_el) {
                auto *light_el = lights_el->FirstChildElement("light");
                while (light_el) {
                    const char *light_name = light_el->Attribute("name");
                    std::string light_name_str = light_name ? light_name : "";

                    TemplateLightBinding *binding = nullptr;
                    for (auto &l : tmpl.lights) {
                        if (l.name == light_name_str) {
                            binding = &l;
                            break;
                        }
                    }

                    if (!binding) {
                        tmpl.lights.push_back({light_name_str, read_light_entry(light_el), {}});
                    } else {
                        binding->alternatives.push_back(read_light_entry(light_el));
                    }

                    light_el = light_el->NextSiblingElement("light");
                }
            }

            // source_labels (legacy, ignored - labels are now stored as device_identifier)

            templates.push_back(std::move(tmpl));
            tmpl_el = tmpl_el->NextSiblingElement("template");
        }

        return templates;
    }

    // save a user template (add or overwrite)
    bool save_user_template(const ControllerTemplate &tmpl) {
        auto path = get_templates_path();
        auto path_tmp = path;
        path_tmp.replace_extension(L"tmp");

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
            // create new document
            auto *decl = doc.NewDeclaration();
            doc.InsertFirstChild(decl);
            auto *root = doc.NewElement("templates");
            doc.InsertEndChild(root);
        }

        auto *root = doc.RootElement();
        if (!root) {
            root = doc.NewElement("templates");
            doc.InsertEndChild(root);
        }

        // remove existing template with same name and game
        auto *existing = root->FirstChildElement("template");
        while (existing) {
            auto *next = existing->NextSiblingElement("template");
            const char *ename = existing->Attribute("name");
            const char *egame = existing->Attribute("game");
            if (ename && egame &&
                std::string(ename) == tmpl.name &&
                std::string(egame) == tmpl.game_name) {
                root->DeleteChild(existing);
            }
            existing = next;
        }

        // create template element
        auto *tmpl_el = doc.NewElement("template");
        tmpl_el->SetAttribute("name", tmpl.name.c_str());
        tmpl_el->SetAttribute("game", tmpl.game_name.c_str());

        // buttons
        auto *buttons_el = doc.NewElement("buttons");
        for (auto &btn : tmpl.buttons) {
            write_button_entry(doc, buttons_el, btn.name, btn.primary);
            for (auto &alt : btn.alternatives) {
                write_button_entry(doc, buttons_el, btn.name, alt);
            }
        }
        tmpl_el->InsertEndChild(buttons_el);

        // keypad buttons
        if (!tmpl.keypad_buttons.empty()) {
            auto *kp_el = doc.NewElement("keypad_buttons");
            for (auto &btn : tmpl.keypad_buttons) {
                write_button_entry(doc, kp_el, btn.name, btn.primary);
                for (auto &alt : btn.alternatives) {
                    write_button_entry(doc, kp_el, btn.name, alt);
                }
            }
            tmpl_el->InsertEndChild(kp_el);
        }

        // analogs
        auto *analogs_el = doc.NewElement("analogs");
        for (auto &a : tmpl.analogs) {
            write_analog(doc, analogs_el, a);
        }
        tmpl_el->InsertEndChild(analogs_el);

        // lights
        auto *lights_el = doc.NewElement("lights");
        for (auto &l : tmpl.lights) {
            write_light_entry(doc, lights_el, l.name, l.primary);
            for (auto &alt : l.alternatives) {
                write_light_entry(doc, lights_el, l.name, alt);
            }
        }
        tmpl_el->InsertEndChild(lights_el);

        // source_labels no longer written (labels are stored as device_identifier)

        root->InsertEndChild(tmpl_el);

        // save atomically via temp file
        if (doc.SaveFile(path_tmp.c_str()) != tinyxml2::XML_SUCCESS) {
            log_warning("templates", "failed to save templates file");
            return false;
        }

        std::error_code ec;
        std::filesystem::rename(path_tmp, path, ec);
        if (ec) {
            log_warning("templates", "failed to rename templates file: {}", ec.message());
            return false;
        }

        return true;
    }

    // delete a user template
    bool delete_user_template(const std::string &game_name, const std::string &template_name) {
        auto path = get_templates_path();

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
            return false;
        }

        auto *root = doc.RootElement();
        if (!root) return false;

        bool found = false;
        auto *tmpl_el = root->FirstChildElement("template");
        while (tmpl_el) {
            auto *next = tmpl_el->NextSiblingElement("template");
            const char *ename = tmpl_el->Attribute("name");
            const char *egame = tmpl_el->Attribute("game");
            if (ename && egame &&
                std::string(ename) == template_name &&
                std::string(egame) == game_name) {
                root->DeleteChild(tmpl_el);
                found = true;
            }
            tmpl_el = next;
        }

        if (found) {
            auto path_tmp = path;
            path_tmp.replace_extension(L"tmp");
            if (doc.SaveFile(path_tmp.c_str()) == tinyxml2::XML_SUCCESS) {
                std::error_code ec;
                std::filesystem::rename(path_tmp, path, ec);
            }
        }

        return found;
    }

    // rename a user template
    bool rename_user_template(const std::string &game_name,
                              const std::string &old_name, const std::string &new_name) {
        auto path = get_templates_path();

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
            return false;
        }

        auto *root = doc.RootElement();
        if (!root) return false;

        bool found = false;
        auto *tmpl_el = root->FirstChildElement("template");
        while (tmpl_el) {
            const char *ename = tmpl_el->Attribute("name");
            const char *egame = tmpl_el->Attribute("game");
            if (ename && egame &&
                std::string(ename) == old_name &&
                std::string(egame) == game_name) {
                tmpl_el->SetAttribute("name", new_name.c_str());
                found = true;
                break;
            }
            tmpl_el = tmpl_el->NextSiblingElement("template");
        }

        if (found) {
            auto path_tmp = path;
            path_tmp.replace_extension(L"tmp");
            if (doc.SaveFile(path_tmp.c_str()) == tinyxml2::XML_SUCCESS) {
                std::error_code ec;
                std::filesystem::rename(path_tmp, path, ec);
            }
        }

        return found;
    }

    // get templates for a specific game (built-in + user)
    std::vector<ControllerTemplate> get_templates_for_game(const std::string &game_name) {
        std::vector<ControllerTemplate> result;

        // add built-in templates
        for (int i = 0; i < BUILTIN_TEMPLATES_COUNT; i++) {
            if (std::string(BUILTIN_TEMPLATES[i].game_name) == game_name) {
                result.push_back(BUILTIN_TEMPLATES[i]);
            }
        }

        // add user templates
        auto user_templates = load_user_templates();
        for (auto &t : user_templates) {
            if (t.game_name == game_name) {
                result.push_back(std::move(t));
            }
        }

        return result;
    }

}
