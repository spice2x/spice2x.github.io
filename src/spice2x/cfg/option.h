#pragma once

#include <string>
#include <utility>
#include <vector>
#include <cstdint>

enum class OptionType {
    Bool,
    Text,
    Integer,
    Enum,
    Hex,
};

struct OptionDefinition {
    std::string title;
    // unique identifier used for flag matching but also stored in config files
    // (should not be changed once published for compat)
    std::string name;
    // what's displayed in the UI/logs as the flag name
    std::string display_name = "";
    // slash-delimited list of strings that also work as flag
    std::string aliases = "";
    // what's displayed in the UI/logs as the tooltip
    std::string desc;
    OptionType type;
    bool hidden = false;
    std::string setting_name = "";
    std::string game_name = "";
    std::string category = "Development";
    bool sensitive = false;
    std::vector<std::pair<std::string, std::string>> elements = {};
    bool disabled = false;
};

class Option {
private:
    OptionDefinition definition;
    std::string search_string;

public:
    std::string value;
    std::vector<Option> alternatives;
    bool disabled = false;

    explicit Option(OptionDefinition definition, std::string value = "") :
        definition(std::move(definition)), value(std::move(value)) {
    };

    inline const OptionDefinition &get_definition() const {
        return this->definition;
    }
    inline void set_definition(OptionDefinition definition) {
        this->definition = std::move(definition);
    }

    inline bool is_active() const {
        return !this->value.empty();
    }

    void value_add(std::string new_value);

    bool has_alternatives() const;
    bool value_bool() const;
    const std::string &value_text() const;
    std::vector<std::string> values() const;
    std::vector<std::string> values_text() const;
    uint32_t value_uint32() const;
    uint64_t value_hex64() const;
    bool search_match(const std::string &query_in_lower_case);
};
