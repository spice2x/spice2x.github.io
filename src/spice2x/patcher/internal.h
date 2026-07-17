#pragma once

#include "patch_manager.h"
#include "external/rapidjson/document.h"
#include "util/nt_loader.h"

namespace patcher {

    // internal engine state (patcher module only)
    extern std::vector<std::string> setting_auto_apply_list;
    extern std::vector<std::string> setting_patches_enabled;
    extern std::map<std::string, std::string> setting_union_patches_enabled;
    extern std::map<std::string, int64_t> setting_int_patches_enabled;
    extern std::filesystem::path LOCAL_PATCHES_PATH;
    extern std::map<std::string, std::vector<std::string>> EXTRA_DLLS;
    extern bool ldr_registered;
    extern void *ldr_notify_cookie;
    extern std::vector<std::string> ldr_target_libraries;

    // internal helpers
    void append_patches(
        std::string& patches_json,
        bool apply_patches = false,
        std::function<bool(const PatchData&)> filter = std::function<bool(const PatchData&)>(),
        std::string pe_identifier_for_patch = "");
    bool is_game_id_wildcard_matched(const std::string& id_from_config);
    std::string getFromUrl(const std::string& dll_name, const std::string& url);
    bool load_from_patches_json(bool apply_patches);
    void load_embedded_patches(bool apply_patches);
    bool import_remote_patches_for_dll(const std::string& url, const std::string& dll_name);
    VOID CALLBACK loader_notification(ULONG reason, PCLDR_DLL_NOTIFICATION_DATA data, PVOID context);

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
