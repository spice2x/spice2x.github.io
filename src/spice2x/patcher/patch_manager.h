#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace patcher {

    enum class PatchType {
        Unknown,
        Memory,
        Signature,
        Union,
        Integer,
    };

    enum class PatchStatus {
        Error,
        Disabled,
        Enabled,
    };

    enum class PatchUrlStatus {
        Valid,
        Invalid,
        Unapplied,
        ValidButNoData,
        Partial,
    };

    struct MemoryPatch {
        std::string dll_name = "";
        std::shared_ptr<uint8_t[]> data_disabled = nullptr;
        size_t data_disabled_len = 0;
        std::shared_ptr<uint8_t[]> data_enabled = nullptr;
        size_t data_enabled_len = 0;
        uint64_t data_offset = 0;
        uint8_t *data_offset_ptr = nullptr;
        bool fatal_error = false;
    };

    struct PatchData;
    struct SignaturePatch {
        std::string dll_name = "";
        std::string signature = "", replacement = "";
        uint64_t offset = 0;
        int64_t usage = 0;

        MemoryPatch to_memory(PatchData *patch);
    };

    struct UnionPatch {
        std::string name = "";
        std::string dll_name = "";
        std::shared_ptr<uint8_t[]> data = nullptr;
        size_t data_len = 0;
        uint64_t offset = 0;
        uint8_t *data_offset_ptr = nullptr;
        bool fatal_error = false;
    };

    struct NumberPatch {
        std::string dll_name = "";
        uint64_t data_offset = 0;
        uint8_t *data_offset_ptr = nullptr;
        int32_t min;
        int32_t max;
        int32_t value;
        size_t size_in_bytes;
        bool fatal_error = false;
    };

    struct PatchGroup {
        std::string id;
        std::string name;
        std::string description;
        std::string caution;
        std::string name_in_lower_case;
    };

    struct PatchData {
        bool enabled;
        std::string game_code;
        int datecode_min = 0;
        int datecode_max = 0;
        std::string name, description, caution;
        std::string name_in_lower_case = "";
        PatchType type;
        bool preset;
        std::vector<MemoryPatch> patches_memory;
        std::vector<UnionPatch> patches_union;
        NumberPatch patch_number;
        PatchGroup group;
        PatchStatus last_status;
        std::string hash;
        bool unverified = false;
        std::string peIdentifier;
        std::string error_reason = "";

        // for union patch only
        std::string selected_union_name = "";
    };

    extern std::optional<std::string> PATCH_MANAGER_CFG_PATH_OVERRIDE;

    std::string get_game_identifier(const std::filesystem::path& dll_path, bool print_info = false);

    // lifecycle: init() sets up the config path and registers for DLL-load
    // notifications; apply_patches_on_start() applies auto-apply patches once the
    // game DLL is loaded. both are safe to call repeatedly.
    void init();
    void apply_patches_on_start();

    // patch load / apply operations (also driven by the overlay window)
    void reload_local_patches(bool apply_patches = false);
    bool import_remote_patches_to_disk();
    void hard_apply_patches();
    void config_load();
    void config_save();
    std::vector<std::string> getExtraDlls(const std::string& firstDll);

    // process-wide patch engine state, rendered and edited by the overlay window
    extern std::filesystem::path config_path;
    extern bool config_dirty;
    extern bool setting_auto_apply;
    extern std::string patch_url;
    extern std::string ACTIVE_JSON_FILE;
    extern std::vector<PatchData> patches;
    extern bool local_patches_initialized;
    extern std::vector<size_t> patches_sorted;
    extern std::string url_fetch_errors;
    extern size_t url_recent_idx;
    extern std::vector<std::string> url_recents;

    PatchStatus is_patch_active(PatchData& patch);
    bool apply_patch(PatchData& patch, bool active);

    std::string displayPath(const std::filesystem::path& path);
}
