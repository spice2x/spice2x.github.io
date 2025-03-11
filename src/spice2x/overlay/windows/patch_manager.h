#pragma once

#include "overlay/window.h"
#include <map>
#include <functional>
#include <filesystem>
#include <optional>
#include "external/rapidjson/document.h"

namespace overlay::windows {

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
        uint8_t* data_offset_ptr = nullptr;
        bool fatal_error = false;
    };

    struct NumberPatch {
        std::string dll_name = "";
        uint64_t data_offset = 0;
        uint8_t* data_offset_ptr = nullptr;
        int32_t min;
        int32_t max;
        int32_t value;
        size_t size_in_bytes;
        bool fatal_error = false;
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
        PatchStatus last_status;
        std::string hash;
        bool unverified = false;
        std::string peIdentifier;
        std::string error_reason = "";

        // for union patch only
        std::string selected_union_name = "";
    };

    extern std::optional<std::string> PATCH_MANAGER_CFG_PATH_OVERRIDE;

    std::string get_game_identifier(const std::filesystem::path& dll_path);

    class PatchManager : public Window {
    public:

        PatchManager(SpiceOverlay *overlay, bool apply_patches = false);
        ~PatchManager() override;

        void build_content() override;
        void reload_local_patches(bool apply_patches = false);
        bool import_remote_patches_to_disk();
        bool load_from_patches_json(bool apply_patches);
        bool import_remote_patches_for_dll(const std::string& url, const std::string& dll_name);
        void hard_apply_patches();
        void load_embedded_patches(bool apply_patches);

    private:

        // configuration
        static std::filesystem::path config_path;
        static bool config_dirty;
        static bool setting_auto_apply;
        static std::vector<std::string> setting_auto_apply_list;
        static std::vector<std::string> setting_patches_enabled;
        static std::map<std::string, std::string> setting_union_patches_enabled;
        static std::map<std::string, int64_t> setting_int_patches_enabled;
        static std::string patch_url;
        static std::string patch_name_filter;

        static std::filesystem::path LOCAL_PATCHES_PATH;
        static std::string ACTIVE_JSON_FILE;

        // patches
        static std::vector<PatchData> patches;
        static bool local_patches_initialized;

        void config_load();
        void config_save();

        void append_patches(
            std::string &patches_json,
            bool apply_patches = false,
            std::function<bool(const PatchData&)> filter = std::function<bool(const PatchData&)>(),
            std::string pe_identifier_for_patch = "");

    };

    PatchStatus is_patch_active(PatchData &patch);
    bool apply_patch(PatchData &patch, bool active);

    int64_t parse_little_endian_int(uint8_t* bytes, size_t size);
    void int_to_little_endian_bytes(int64_t value, uint8_t* bytes, size_t size);

    std::vector<uint8_t>* find_in_dll_map(
        const std::string& dll_name, size_t offset, size_t size);
    std::vector<uint8_t>* find_in_dll_map_org(
        const std::string& dll_name, size_t offset, size_t size);

    bool restore_bytes_from_dll_map_org(
        uint8_t* destination, const std::string& dll_name, size_t offset, size_t size);

    void create_dll_backup(
        std::vector<std::string>& written_list, const std::filesystem::path& dll_path);
    std::string fix_up_dll_name(const std::string& dll_name);
    uint8_t* get_dll_offset_for_patch_apply(
        const std::string& dll_name, const uint64_t data_offset, const size_t size_in_bytes);
 
    uint64_t parse_json_data_offset(
        const std::string &patch_name, const rapidjson::Value &value);

    void print_auto_apply_status(PatchData &patch);
}
