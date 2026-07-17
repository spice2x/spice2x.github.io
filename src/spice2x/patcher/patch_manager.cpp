#include "internal.h"

#include <algorithm>
#include <windows.h>

#include "avs/game.h"
#include "cfg/configurator.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/utils.h"

namespace patcher {

    std::optional<std::string> PATCH_MANAGER_CFG_PATH_OVERRIDE;

    std::filesystem::path config_path;
    bool config_dirty = false;
    bool setting_auto_apply = false;

    std::vector<std::string> setting_auto_apply_list;
    std::vector<std::string> setting_patches_enabled;

    std::map<std::string, std::string> setting_union_patches_enabled;
    std::map<std::string, int64_t> setting_int_patches_enabled;

    std::string patch_url;

    std::filesystem::path LOCAL_PATCHES_PATH("patches");
    std::string ACTIVE_JSON_FILE;

    std::vector<PatchData> patches;
    bool local_patches_initialized = false;
    std::vector<size_t> patches_sorted;
    std::map<std::string, std::vector<std::string>> EXTRA_DLLS = {
        {"jubeat.dll", {"music_db.dll", "coin.dll"}},
        {"arkmdxp3.dll", {"gamemdx.dll"}},
        {"arkmdxp4.dll", {"gamemdx.dll"}},
        {"arkmdxbio2.dll", {"gamemdx.dll"}},
        {"arkndd.dll", {"gamendd.dll"}},
        {"arkkep.dll", {"game.dll"}},
        {"arkjc9.dll", {"gamejc9.dll"}},
        {"arkkdm.dll", {"gamekdm.dll"}},
        {"arkmmd.dll", {"gamemmd.dll"}},
        {"arkklp.dll", {"lpac.dll"}},
        {"arknck.dll", {"weac.dll"}},
        {"gdxg.dll", {"game.dll", "libshare-pj.dll", "boot.dll"}}
    };

    std::string url_fetch_errors;
    size_t url_recent_idx = -1;
    std::vector<std::string> url_recents;

    bool ldr_registered = false;
    void *ldr_notify_cookie = nullptr;
    std::vector<std::string> ldr_target_libraries;

    void init() {
        if (PATCH_MANAGER_CFG_PATH_OVERRIDE.has_value()) {
            config_path = PATCH_MANAGER_CFG_PATH_OVERRIDE.value();
            log_info("patchmanager", "using custom config file path: {}", config_path);
        } else {
            config_path =
                fileutils::get_config_file_path("patchmanager", "spicetools_patch_manager.json");
        }

        // register for DLL load notifications so patches can be (re)applied as
        // the game's target libraries come into memory. registration happens
        // once and is left in place for the lifetime of the process.
        if (!ldr_registered) {
            ldr_target_libraries = getExtraDlls(avs::game::DLL_NAME);
            ldr_target_libraries.push_back(avs::game::DLL_NAME);

            const auto register_fn = reinterpret_cast<decltype(&LdrRegisterDllNotification)>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));

            if (register_fn && NT_SUCCESS(register_fn(
                    0, &loader_notification, nullptr, &ldr_notify_cookie))) {
                log_info("patchmanager", "registered for DLL load notifications");
            } else {
                log_warning("patchmanager", "failed to register for DLL load notifications");
            }

            ldr_registered = true;
        }

        if (!local_patches_initialized) {
            patch_url.clear();
            if (fileutils::file_exists(config_path)) {
                config_load();
            }
            if (cfg::CONFIGURATOR_STANDALONE) {
                reload_local_patches(true);
            }
        }
    }

    void apply_patches_on_start() {
        if (!local_patches_initialized) {
            reload_local_patches(true);
        }
    }

    VOID CALLBACK loader_notification(
        ULONG reason,
        PCLDR_DLL_NOTIFICATION_DATA data,
        PVOID) {

        if (reason == LDR_DLL_NOTIFICATION_REASON_LOADED) {
            const auto dll = strtolower(std::filesystem::path({
                data->Loaded.FullDllName->Buffer,
                data->Loaded.FullDllName->Length / sizeof(wchar_t)
            }).filename().string());

            if (std::ranges::find(ldr_target_libraries, dll) != ldr_target_libraries.end()) {
                reload_local_patches(true);
            }
        }
    }
}
