#pragma once

#include <set>
#include <string>
#include <vector>

#include "cfg/button.h"

namespace overlay::windows {

    // single button binding entry (primary or alternative)
    struct TemplateButtonEntry {
        unsigned short vKey = INVALID_VKEY;
        ButtonAnalogType analog_type = BAT_NONE;
        std::string device_identifier;
        bool invert = false;
        double debounce_up = 0.0;
        double debounce_down = 0.0;
        unsigned short velocity_threshold = 0;

        bool is_naive() const { return device_identifier.empty() && vKey != INVALID_VKEY; }
        bool is_device() const { return !device_identifier.empty(); }
        bool is_unbound() const { return device_identifier.empty() && vKey == INVALID_VKEY; }
    };

    // button binding with primary + alternatives
    struct TemplateButtonBinding {
        std::string name;
        TemplateButtonEntry primary;
        std::vector<TemplateButtonEntry> alternatives;
    };

    // analog binding
    struct TemplateAnalogBinding {
        std::string name;
        std::string device_identifier;
        unsigned short index = 0xFF;
        float sensitivity = 1.f;
        float deadzone = 0.f;
        bool deadzone_mirror = false;
        bool invert = false;
        bool smoothing = false;
        int multiplier = 1;
        bool relative_mode = false;
        int delay_buffer_depth = 0;

        bool is_device() const { return !device_identifier.empty(); }
        bool is_unbound() const { return device_identifier.empty() && index == 0xFF; }
    };

    // single light binding entry (primary or alternative)
    struct TemplateLightEntry {
        std::string device_identifier;
        unsigned int index = 0;

        bool is_device() const { return !device_identifier.empty(); }
        bool is_unbound() const { return device_identifier.empty(); }
    };

    // light binding with primary + alternatives
    struct TemplateLightBinding {
        std::string name;
        TemplateLightEntry primary;
        std::vector<TemplateLightEntry> alternatives;
    };

    // controller preset
    struct ControllerTemplate {
        std::string name;
        std::string game_name;
        bool is_builtin = false;
        std::vector<TemplateButtonBinding> buttons;
        std::vector<TemplateButtonBinding> keypad_buttons;
        std::vector<TemplateAnalogBinding> analogs;
        std::vector<TemplateLightBinding> lights;

        // helpers
        int count_naive_buttons() const;
        int count_device_buttons() const;
        int count_device_analogs() const;
        int count_device_lights() const;
        std::set<std::string> get_used_devices() const;

        // get all binding sources (device_identifier strings used in this preset)
        std::vector<std::string> get_binding_sources() const;

        // get summary of bindings for a specific source (for tooltip display)
        std::string get_source_summary(const std::string &source) const;

        // replace all occurrences of a source device_identifier with a new one
        void rename_source(const std::string &old_id, const std::string &new_id);
    };

    // template management API
    std::vector<ControllerTemplate> get_templates_for_game(const std::string &game_name);
    std::vector<ControllerTemplate> load_user_templates();
    bool save_user_template(const ControllerTemplate &tmpl);
    bool delete_user_template(const std::string &game_name, const std::string &template_name);
    bool rename_user_template(const std::string &game_name,
                              const std::string &old_name, const std::string &new_name);
}
