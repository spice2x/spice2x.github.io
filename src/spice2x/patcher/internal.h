#pragma once

#include "patch_manager.h"
#include "external/rapidjson/document.h"

namespace patcher {
    std::string patch_hash(PatchData& patch);
    void clear_dll_maps();

    int64_t parse_little_endian_int(uint8_t *bytes, size_t size);
    void int_to_little_endian_bytes(int64_t value, uint8_t *bytes, size_t size);

    std::vector<uint8_t> *find_in_dll_map(
        const std::string& dll_name, size_t offset, size_t size);
    std::vector<uint8_t> *find_in_dll_map_org(
        const std::string& dll_name, size_t offset, size_t size);

    bool restore_bytes_from_dll_map_org(
        uint8_t *destination, const std::string& dll_name, size_t offset, size_t size);

    void create_dll_backup(
        std::vector<std::filesystem::path>& written_list, const std::filesystem::path& dll_path);
    std::string fix_up_dll_name(const std::string& dll_name);
    uint8_t *get_dll_offset_for_patch_apply(
        const std::string& dll_name, uint64_t data_offset, size_t size_in_bytes);

    uint64_t parse_json_data_offset(
        const std::string& patch_name, const rapidjson::Value& value);

    void print_auto_apply_status(PatchData& patch);
}
