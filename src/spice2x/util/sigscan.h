#pragma once

#include <algorithm>
#include <string>
#include <cstdint>
#include <vector>
#include <filesystem>
#include "windows.h"
#include "psapi.h"

intptr_t find_pattern(
        std::vector<unsigned char> &data,
        intptr_t base,
        const unsigned char *pattern,
        const char *mask,
        intptr_t offset,
        intptr_t usage);

intptr_t find_pattern(
        HMODULE module,
        const unsigned char *pattern,
        const char *mask,
        intptr_t offset,
        intptr_t usage);

intptr_t find_pattern(
        HMODULE module,
        const std::string &pattern,
        const char *mask,
        intptr_t offset,
        intptr_t result_usage);

intptr_t find_pattern_from(
        std::vector<unsigned char> &data,
        intptr_t base,
        const unsigned char *pattern,
        const char *mask,
        intptr_t offset,
        intptr_t usage,
        intptr_t start_from);

intptr_t find_pattern_from(
        HMODULE module,
        const unsigned char *pattern,
        const char *mask,
        intptr_t offset,
        intptr_t usage,
        intptr_t start_from);

intptr_t find_pattern_from(
        HMODULE module,
        const std::string &pattern,
        const char *mask,
        intptr_t offset,
        intptr_t result_usage,
        intptr_t start_from);

intptr_t replace_pattern(
        HMODULE module,
        const unsigned char *pattern,
        const char *mask,
        intptr_t offset,
        intptr_t usage,
        const unsigned char *replace_data,
        const char *replace_mask);

intptr_t replace_pattern(
        HMODULE module,
        const std::string &signature,
        const std::string &replacement,
        intptr_t offset,
        intptr_t usage);

bool get_pe_identifier(const std::filesystem::path& dll_path, uint32_t* time_date_stamp, uint32_t* address_of_entry_point);