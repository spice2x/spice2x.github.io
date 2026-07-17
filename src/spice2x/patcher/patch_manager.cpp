#include "patch_manager.h"

#include <algorithm>

#include "avs/game.h"
#include "cfg/configurator.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/utils.h"

namespace patcher {

    std::filesystem::path PatchManager::config_path;
    std::optional<std::string> PATCH_MANAGER_CFG_PATH_OVERRIDE;

    bool PatchManager::config_dirty = false;
    bool PatchManager::setting_auto_apply = false;

    std::vector<std::string> PatchManager::setting_auto_apply_list;
    std::vector<std::string> PatchManager::setting_patches_enabled;

    std::map<std::string, std::string> PatchManager::setting_union_patches_enabled;
    std::map<std::string, int64_t> PatchManager::setting_int_patches_enabled;

    std::string PatchManager::patch_url("");

    std::filesystem::path PatchManager::LOCAL_PATCHES_PATH("patches");
    std::string PatchManager::ACTIVE_JSON_FILE("");

    std::vector<PatchData> PatchManager::patches;
    bool PatchManager::local_patches_initialized = false;
    std::vector<size_t> PatchManager::patches_sorted;
    std::map<std::string, std::vector<std::string>> PatchManager::EXTRA_DLLS = {
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

    std::string PatchManager::url_fetch_errors;
    size_t PatchManager::url_recent_idx = -1;
    std::vector<std::string> PatchManager::url_recents = {};

    bool PatchManager::ldr_registered = false;
    void *PatchManager::ldr_notify_cookie = nullptr;
    std::vector<std::string> PatchManager::ldr_target_libraries;

    PatchManager::PatchManager() {
        if (PATCH_MANAGER_CFG_PATH_OVERRIDE.has_value()) {
            config_path = PATCH_MANAGER_CFG_PATH_OVERRIDE.value();
            log_info("patchmanager", "using custom config file path: {}", config_path);
        } else {
            config_path =
                fileutils::get_config_file_path("patchmanager", "spicetools_patch_manager.json");
        }

        if (!ldr_registered) {
            ldr_target_libraries = getExtraDlls(avs::game::DLL_NAME);
            ldr_target_libraries.push_back(avs::game::DLL_NAME);

            const auto register_fn = reinterpret_cast<decltype(&LdrRegisterDllNotification)>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));

            if (register_fn && NT_SUCCESS(register_fn(
                    0, &PatchManager::loader_notification, this, &ldr_notify_cookie))) {
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

    void PatchManager::apply_patches_on_start() {
        if (!local_patches_initialized) {
            reload_local_patches(true);
        }
    }

    VOID CALLBACK PatchManager::loader_notification(
        ULONG reason,
        PCLDR_DLL_NOTIFICATION_DATA data,
        PVOID context) {

        if (reason == LDR_DLL_NOTIFICATION_REASON_LOADED) {
            const auto dll = strtolower(std::filesystem::path({
                data->Loaded.FullDllName->Buffer,
                data->Loaded.FullDllName->Length / sizeof(wchar_t)
            }).filename().string());

            if (std::ranges::find(ldr_target_libraries, dll) != ldr_target_libraries.end()) {
                static_cast<PatchManager *>(context)->reload_local_patches(true);
            }
        }
    }
}
