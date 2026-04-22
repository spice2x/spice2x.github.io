#include "overlay/windows/controller_presets.h"

#include <filesystem>

#include "cfg/resource.h"
#include "external/tinyxml2/tinyxml2.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/resutils.h"

namespace overlay::windows {

    static std::filesystem::path get_templates_path() {
        return fileutils::get_config_file_path("ControllerPresets", "spicetools_presets.xml");
    }

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
        el->SetAttribute("bat_threshold", entry.bat_threshold);
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

        int bat = 0;
        el->QueryIntAttribute("bat_threshold", &bat);
        entry.bat_threshold = bat;

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

        int idx = 0xFF;
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

    // Parse all templates from an already-loaded XMLDocument.
    // is_builtin controls the is_builtin flag on each returned template.
    static std::vector<ControllerTemplate> load_templates_from_doc(
            tinyxml2::XMLDocument &doc, bool is_builtin) {
        std::vector<ControllerTemplate> templates;

        auto *root = doc.RootElement();
        if (!root) {
            return templates;
        }

        auto *tmpl_el = root->FirstChildElement("template");
        while (tmpl_el) {
            ControllerTemplate tmpl;
            const char *name = tmpl_el->Attribute("name");
            const char *game = tmpl_el->Attribute("game");
            tmpl.name = name ? name : "";
            tmpl.game_name = game ? game : "";
            tmpl.is_builtin = is_builtin;

            // buttons
            auto *buttons_el = tmpl_el->FirstChildElement("buttons");
            if (buttons_el) {
                auto *btn_el = buttons_el->FirstChildElement("button");
                while (btn_el) {
                    const char *btn_name = btn_el->Attribute("name");
                    std::string btn_name_str = btn_name ? btn_name : "";

                    TemplateButtonBinding *binding = nullptr;
                    for (auto &b : tmpl.buttons) {
                        if (b.name == btn_name_str) { binding = &b; break; }
                    }
                    if (!binding) {
                        tmpl.buttons.push_back({btn_name_str, read_button_entry(btn_el), {}});
                    } else {
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
                        if (b.name == btn_name_str) { binding = &b; break; }
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
                        if (l.name == light_name_str) { binding = &l; break; }
                    }
                    if (!binding) {
                        tmpl.lights.push_back({light_name_str, read_light_entry(light_el), {}});
                    } else {
                        binding->alternatives.push_back(read_light_entry(light_el));
                    }
                    light_el = light_el->NextSiblingElement("light");
                }
            }

            templates.push_back(std::move(tmpl));
            tmpl_el = tmpl_el->NextSiblingElement("template");
        }

        return templates;
    }

    static std::vector<ControllerTemplate> load_builtin_templates() {
        auto xml = resutil::load_file_string(IDR_CONTROLLER_PRESETS_BUILTIN);
        tinyxml2::XMLDocument doc;
        auto err = doc.Parse(xml.c_str());
        if (err != tinyxml2::XML_SUCCESS) {
            log_warning("templates", "failed to parse builtin templates XML: {}", (int)err);
            return {};
        }
        return load_templates_from_doc(doc, true);
    }

    std::vector<ControllerTemplate> load_user_templates() {
        std::vector<ControllerTemplate> templates;
        auto path = get_templates_path();

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
            return templates;
        }
        return load_templates_from_doc(doc, false);
    }

    bool save_user_template(
        const ControllerTemplate &tmpl,
        const bool save_buttons,
        const bool save_keypads,
        const bool save_analogs,
        const bool save_lights) {

        auto path = get_templates_path();
        auto path_tmp = path;
        path_tmp.replace_extension(L"tmp");

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
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

        auto *tmpl_el = doc.NewElement("template");
        tmpl_el->SetAttribute("name", tmpl.name.c_str());
        tmpl_el->SetAttribute("game", tmpl.game_name.c_str());

        // buttons — skip unbound entries
        auto *buttons_el = doc.NewElement("buttons");
        if (save_buttons) {
            for (auto &btn : tmpl.buttons) {
                if (!btn.primary.is_unbound()) {
                    write_button_entry(doc, buttons_el, btn.name, btn.primary);
                }
                for (auto &alt : btn.alternatives) {
                    if (!alt.is_unbound()) {
                        write_button_entry(doc, buttons_el, btn.name, alt);
                    }
                }
            }
        }
        tmpl_el->InsertEndChild(buttons_el);

        // keypad buttons — skip unbound entries
        if (!tmpl.keypad_buttons.empty()) {
            auto *kp_el = doc.NewElement("keypad_buttons");
            if (save_keypads) {
                for (auto &btn : tmpl.keypad_buttons) {
                    if (!btn.primary.is_unbound()) {
                        write_button_entry(doc, kp_el, btn.name, btn.primary);
                    }
                    for (auto &alt : btn.alternatives) {
                        if (!alt.is_unbound()) {
                            write_button_entry(doc, kp_el, btn.name, alt);
                        }
                    }
                }
            }
            tmpl_el->InsertEndChild(kp_el);
        }

        // analogs — skip unbound entries
        auto *analogs_el = doc.NewElement("analogs");
        if (save_analogs) {
            for (auto &a : tmpl.analogs) {
                if (!a.is_unbound()) {
                    write_analog(doc, analogs_el, a);
                }
            }
        }
        tmpl_el->InsertEndChild(analogs_el);

        // lights — skip unbound entries
        auto *lights_el = doc.NewElement("lights");
        if (save_lights) {
            for (auto &l : tmpl.lights) {
                if (!l.primary.is_unbound()) {
                    write_light_entry(doc, lights_el, l.name, l.primary);
                }
                for (auto &alt : l.alternatives) {
                    if (!alt.is_unbound()) {
                        write_light_entry(doc, lights_el, l.name, alt);
                    }
                }
            }
        }
        tmpl_el->InsertEndChild(lights_el);

        root->InsertEndChild(tmpl_el);

        // ensure directory exists
        if (!path.parent_path().empty() && !std::filesystem::exists(path.parent_path())) {
            fileutils::dir_create_recursive(path.parent_path());
        }

        // save atomically via temp file
        auto save_err = doc.SaveFile(path_tmp.c_str());
        if (save_err != tinyxml2::XML_SUCCESS) {
            log_warning("templates", "failed to save templates file: {}", (int)save_err);
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

    bool delete_user_template(const std::string &game_name, const std::string &template_name) {
        auto path = get_templates_path();

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) return false;

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

    bool rename_user_template(const std::string &game_name,
                              const std::string &old_name, const std::string &new_name) {
        auto path = get_templates_path();

        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) return false;

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

    std::vector<ControllerTemplate> get_templates_for_game(const std::string &game_name) {
        std::vector<ControllerTemplate> result;

        log_misc("templates", "loading built-in templates for {}", game_name);
        for (auto &t : load_builtin_templates()) {
            if (t.game_name == game_name) {
                result.push_back(std::move(t));
            }
        }

        log_misc("templates", "loading user templates for {}", game_name);
        for (auto &t : load_user_templates()) {
            if (t.game_name == game_name) {
                result.push_back(std::move(t));
            }
        }

        return result;
    }

}
