#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "util/nt_loader.h"

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

    std::string get_game_identifier(const std::filesystem::path& dll_path, bool print_info = false);

    class PatchManager {
    public:
        PatchManager();

        void apply_patches_on_start();
        void reload_local_patches(bool apply_patches = false);
        bool import_remote_patches_to_disk();
        bool load_from_patches_json(bool apply_patches);
        bool import_remote_patches_for_dll(const std::string& url, const std::string& dll_name);
        void hard_apply_patches();
        void load_embedded_patches(bool apply_patches);

    protected:
        void config_load();
        void config_save();
        void append_patches(
            std::string& patches_json,
            bool apply_patches = false,
            std::function<bool(const PatchData&)> filter = std::function<bool(const PatchData&)>(),
            std::string pe_identifier_for_patch = "");
        bool is_game_id_wildcard_matched(const std::string& id_from_config);
        static std::vector<std::string> getExtraDlls(const std::string& firstDll);
        static std::string getFromUrl(const std::string& dll_name, const std::string& url);

        static std::filesystem::path config_path;
        static bool config_dirty;
        static bool setting_auto_apply;
        static std::vector<std::string> setting_auto_apply_list;
        static std::vector<std::string> setting_patches_enabled;
        static std::map<std::string, std::string> setting_union_patches_enabled;
        static std::map<std::string, int64_t> setting_int_patches_enabled;
        static std::string patch_url;

        static std::filesystem::path LOCAL_PATCHES_PATH;
        static std::string ACTIVE_JSON_FILE;

        static std::vector<PatchData> patches;
        static bool local_patches_initialized;
        static std::vector<size_t> patches_sorted;
        static std::map<std::string, std::vector<std::string>> EXTRA_DLLS;

        static std::string url_fetch_errors;
        static size_t url_recent_idx;
        static std::vector<std::string> url_recents;

        static bool ldr_registered;
        static void *ldr_notify_cookie;
        static std::vector<std::string> ldr_target_libraries;
        static VOID CALLBACK loader_notification(
            ULONG reason,
            PCLDR_DLL_NOTIFICATION_DATA data,
            PVOID context);
    };

    PatchStatus is_patch_active(PatchData& patch);
    bool apply_patch(PatchData& patch, bool active);

    std::string displayPath(const std::filesystem::path& path);
}
