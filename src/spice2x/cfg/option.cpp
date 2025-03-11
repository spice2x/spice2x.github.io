#include "option.h"

#include "util/logging.h"
#include "util/utils.h"

void Option::value_add(std::string new_value) {

    // put in primary slot if possible
    if (this->value.empty()) {
        this->value = std::move(new_value);
        return;
    }

    // add new alternative
    this->alternatives.emplace_back(this->definition, std::move(new_value));
}

bool Option::has_alternatives() const {
    return !this->alternatives.empty();
}

bool Option::value_bool() const {
    if (this->definition.type != OptionType::Bool) {
        log_fatal("option", "value_bool() called on {}/{}", this->definition.title, this->definition.type);
    }
    return !this->value.empty();
}

const std::string &Option::value_text() const {
    if (this->definition.type != OptionType::Text && this->definition.type != OptionType::Enum) {
        log_fatal("option", "value_text() called on {}/{}", this->definition.title, this->definition.type);
    }
    return this->value;
}

std::vector<std::string> Option::values() const {
    std::vector<std::string> values;

    if (!this->is_active()) {
        return values;
    }

    values.push_back(this->value);

    for (auto &alt : this->alternatives) {
        if (alt.is_active()) {
            values.push_back(alt.value);
        }
    }

    return values;
}

std::vector<std::string> Option::values_text() const {
    std::vector<std::string> values;

    if (!this->is_active()) {
        return values;
    }

    values.push_back(this->value_text());

    for (auto &alt : this->alternatives) {
        if (alt.is_active()) {
            values.push_back(alt.value_text());
        }
    }

    return values;
}

uint32_t Option::value_uint32() const {
    if (this->definition.type != OptionType::Integer && this->definition.type != OptionType::Enum) {
        log_fatal("option", "invalid call: value_uint32() called on {}/{}", this->definition.title, this->definition.type);
        return 0;
    }
    char *p;
    auto res = strtol(this->value.c_str(), &p, 10);
    if (*p) {
        log_fatal("option", "failed to convert {} to unsigned integer (option: {})", this->value, this->definition.title);
        return 0;
    } else {
        return res;
    }
}

uint64_t Option::value_hex64() const {
    if (this->definition.type != OptionType::Hex) {
        log_fatal("option", "invalid call: value_hex() called on {}/{}", this->definition.title, this->definition.type);
        return 0;
    }

    uint64_t affinity = 0;
    try {
        affinity = std::stoull(this->value.c_str(), nullptr, 16);
    } catch (const std::exception &ex) {
        log_fatal("option", "failed to parse {} as hexadecimal (option: {})", this->value, this->definition.title);
    }
    return affinity;
}

bool Option::search_match(const std::string &query_in_lower_case) {
    if (this->search_string.empty()) {
        const auto &param =
            this->definition.display_name.empty() ?
                this->definition.name : this->definition.display_name;

        const auto s = this->definition.title + " -" + param;
        this->search_string = strtolower(s);
    }
    return this->search_string.find(query_in_lower_case) != std::string::npos;
}
