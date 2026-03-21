#include "controller_presets.h"

#include <algorithm>

#include "cfg/button.h"

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
            if (a.is_device()) {
                sources_set.insert(a.device_identifier);
            }
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

}
