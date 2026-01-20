#include "cfg/configurator.h"
#include "external/nvapi/nvapi.h"
#include "external/nvapi/NvApiDriverSettings.h"
#include "util/libutils.h"
#include "util/logging.h"

#include "nvapi.h"

typedef uintptr_t *(*NvAPI_QueryInterface_t)(unsigned int);
typedef NvAPI_Status (*NvAPI_Initialize_t)();
typedef NvAPI_Status (*NvAPI_Unload_t)();
typedef NvAPI_Status (*NvAPI_GetErrorMessage_t)(NvAPI_Status, NvAPI_ShortString);
typedef NvAPI_Status (*NvAPI_DRS_CreateSession_t)(NvDRSSessionHandle *);
typedef NvAPI_Status (*NvAPI_DRS_LoadSettings_t)(NvDRSSessionHandle);
typedef NvAPI_Status (*NvAPI_DRS_FindProfileByName_t)(NvDRSSessionHandle, NvAPI_UnicodeString, NvDRSProfileHandle *);
typedef NvAPI_Status (*NvAPI_DRS_FindApplicationByName_t)(NvDRSSessionHandle, NvAPI_UnicodeString, NvDRSProfileHandle *, NVDRS_APPLICATION *);
typedef NvAPI_Status (*NvAPI_DRS_CreateProfile_t)(NvDRSSessionHandle, NVDRS_PROFILE *, NvDRSProfileHandle *);
typedef NvAPI_Status (*NvAPI_DRS_CreateApplication_t)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_APPLICATION *);
typedef NvAPI_Status (*NvAPI_DRS_GetSetting_t)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING *);
typedef NvAPI_Status (*NvAPI_DRS_SetSetting_t)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING *);
typedef NvAPI_Status (*NvAPI_DRS_SaveSettings_t)(NvDRSSessionHandle);
typedef NvAPI_Status (*NvAPI_DRS_DestroySession_t)(NvDRSSessionHandle);

namespace nvapi {

    bool ADD_PROFILE = false;

    static bool NVAPI_MODULE_LOAD_SUCCEEDED = false;
    static HMODULE nvapi = nullptr;
    static NvAPI_QueryInterface_t NvAPI_QueryInterface = nullptr;
    static NvAPI_Initialize_t NvAPI_Initialize = nullptr;
    static NvAPI_Unload_t NvAPI_Unload = nullptr;
    static NvAPI_GetErrorMessage_t NvAPI_GetErrorMessage = nullptr;
    static NvAPI_DRS_CreateSession_t NvAPI_DRS_CreateSession = nullptr;
    static NvAPI_DRS_LoadSettings_t NvAPI_DRS_LoadSettings = nullptr;
    static NvAPI_DRS_FindProfileByName_t NvAPI_DRS_FindProfileByName = nullptr;
    static NvAPI_DRS_FindApplicationByName_t NvAPI_DRS_FindApplicationByName = nullptr;
    static NvAPI_DRS_CreateProfile_t NvAPI_DRS_CreateProfile = nullptr;
    static NvAPI_DRS_CreateApplication_t NvAPI_DRS_CreateApplication = nullptr;
    static NvAPI_DRS_GetSetting_t NvAPI_DRS_GetSetting = nullptr;
    static NvAPI_DRS_SetSetting_t NvAPI_DRS_SetSetting = nullptr;
    static NvAPI_DRS_SaveSettings_t NvAPI_DRS_SaveSettings = nullptr;
    static NvAPI_DRS_DestroySession_t NvAPI_DRS_DestroySession = nullptr;

    char *get_error_message(NvAPI_Status status) {
        static NvAPI_ShortString sz_desc = { 0 };
        nvapi::NvAPI_GetErrorMessage(status, sz_desc);
        return sz_desc;
    }

    void initialize() {
        // do not attempt to load nvapi64.dll or nvapi.dll unless the user enabled the option.
        // on non-NVIDIA GPUs, any of the calls into the DLL may crash, including LoadLibrary and NvAPI_QueryInterface.
        if (cfg::CONFIGURATOR_STANDALONE || !ADD_PROFILE) {
            return;
        }

#ifdef SPICE64
        log_misc("nvapi", "Loading nvapi64.dll ...");
        nvapi = libutils::try_library("nvapi64");
#else
        log_misc("nvapi", "Loading nvapi.dll ...");
        nvapi = libutils::try_library("nvapi");
#endif
        if (nvapi == nullptr) {
            log_warning("nvapi", "NVAPI DLL not available");
            return;
        }
        log_misc("nvapi", "NVAPI DLL loaded.");

        log_misc("nvapi", "Find address of nvapi_QueryInterface...");
        NvAPI_QueryInterface = reinterpret_cast<NvAPI_QueryInterface_t>(libutils::try_proc(nvapi, "nvapi_QueryInterface"));
        if (!NvAPI_QueryInterface) {
            log_warning("nvapi", "nvapi_QueryInterface not found");
            return;
        }

        log_misc("nvapi", "Calling NvAPI_Initialize ...");
        // Thanks to 3Dmigoto for these consts
        // https://raw.githubusercontent.com/bo3b/3Dmigoto/master/NVAPI/DllMain.cpp
        NvAPI_Initialize = reinterpret_cast<NvAPI_Initialize_t>(NvAPI_QueryInterface(0x0150E828));
        if (!NvAPI_Initialize) {
            log_warning("nvapi", "NvAPI_Initialize not found");
            return;
        }
        if (auto status = NvAPI_Initialize(); status != NVAPI_OK) {
            log_warning("nvapi", "NvAPI_Initialize failed: {}", get_error_message(status));
            return;
        }

        log_misc("nvapi", "NvAPI_Initialize succeeded");

        NvAPI_Unload = reinterpret_cast<NvAPI_Unload_t>(NvAPI_QueryInterface(0xD22BDD7E));
        if (!NvAPI_Unload) {
            log_warning("nvapi", "NvAPI_Unload not found");
            return;
        }
        NvAPI_GetErrorMessage = reinterpret_cast<NvAPI_GetErrorMessage_t>(NvAPI_QueryInterface(0x6C2D048C));
        if (!NvAPI_GetErrorMessage) {
            log_warning("nvapi", "NvAPI_GetErrorMessage not found");
            return;
        }
        NvAPI_DRS_CreateSession = reinterpret_cast<NvAPI_DRS_CreateSession_t>(NvAPI_QueryInterface(0x0694D52E));
        if (!NvAPI_DRS_CreateSession) {
            log_warning("nvapi", "NvAPI_DRS_CreateSession not found");
            return;
        }
        NvAPI_DRS_LoadSettings = reinterpret_cast<NvAPI_DRS_LoadSettings_t>(NvAPI_QueryInterface(0x375DBD6B));
        if (!NvAPI_DRS_LoadSettings) {
            log_warning("nvapi", "NvAPI_DRS_LoadSettings not found");
            return;
        }
        NvAPI_DRS_FindProfileByName = reinterpret_cast<NvAPI_DRS_FindProfileByName_t>(NvAPI_QueryInterface(0x7E4A9A0B));
        if (!NvAPI_DRS_FindProfileByName) {
            log_warning("nvapi", "NvAPI_DRS_FindProfileByName not found");
            return;
        }
        NvAPI_DRS_FindApplicationByName = reinterpret_cast<NvAPI_DRS_FindApplicationByName_t>(NvAPI_QueryInterface(0xEEE566B2));
        if (!NvAPI_DRS_FindApplicationByName) {
            log_warning("nvapi", "NvAPI_DRS_FindApplicationByName not found");
            return;
        }
        NvAPI_DRS_CreateProfile = reinterpret_cast<NvAPI_DRS_CreateProfile_t>(NvAPI_QueryInterface(0x0CC176068));
        if (!NvAPI_DRS_CreateProfile) {
            log_warning("nvapi", "NvAPI_DRS_CreateProfile not found");
            return;
        }
        NvAPI_DRS_CreateApplication = reinterpret_cast<NvAPI_DRS_CreateApplication_t>(NvAPI_QueryInterface(0x4347A9DE));
        if (!NvAPI_DRS_CreateApplication) {
            log_warning("nvapi", "NvAPI_DRS_CreateApplication not found");
            return;
        }
        NvAPI_DRS_GetSetting = reinterpret_cast<NvAPI_DRS_GetSetting_t>(NvAPI_QueryInterface(0x73BF8338));
        if (!NvAPI_DRS_GetSetting) {
            log_warning("nvapi", "NvAPI_DRS_GetSetting not found");
            return;
        }
        NvAPI_DRS_SetSetting = reinterpret_cast<NvAPI_DRS_SetSetting_t>(NvAPI_QueryInterface(0x577DD202));
        if (!NvAPI_DRS_SetSetting) {
            log_warning("nvapi", "NvAPI_DRS_SetSetting not found");
            return;
        }
        NvAPI_DRS_SaveSettings = reinterpret_cast<NvAPI_DRS_SaveSettings_t>(NvAPI_QueryInterface(0xFCBC7E14));
        if (!NvAPI_DRS_SaveSettings) {
            log_warning("nvapi", "NvAPI_DRS_SaveSettings not found");
            return;
        }
        NvAPI_DRS_DestroySession = reinterpret_cast<NvAPI_DRS_DestroySession_t>(NvAPI_QueryInterface(0x0DAD9CFF8));
        if (!NvAPI_DRS_DestroySession) {
            log_warning("nvapi", "NvAPI_DRS_DestroySession not found");
            return;
        }

        NVAPI_MODULE_LOAD_SUCCEEDED = true;
        log_misc("nvapi", "NVAPI module initialized");
    }

    NvAPI_Status create_profile_if_needed(NvDRSSessionHandle h_session, NvDRSProfileHandle *h_profile) {
        NvAPI_UnicodeString app_name;
#ifdef SPICE64
        memcpy_s(app_name, sizeof(app_name), L"spice64.exe", 12 * sizeof(wchar_t));
#else
        memcpy_s(app_name, sizeof(app_name), L"spice.exe", 10 * sizeof(wchar_t));
#endif
        auto application = NVDRS_APPLICATION {};
        application.version = NVDRS_APPLICATION_VER;
        auto status = NvAPI_DRS_FindApplicationByName(h_session, app_name, h_profile, &application);
        if (status == NVAPI_OK) {
            return status;
        }
        if (status != NVAPI_EXECUTABLE_NOT_FOUND) {
            log_warning("nvapi", "error finding application profile: {}", get_error_message(status));
            return status;
        }

        auto profile = NVDRS_PROFILE {};
        profile.version = NVDRS_PROFILE_VER;
        memcpy_s(profile.profileName, sizeof(profile.profileName), L"SpiceTools", 11 * sizeof(wchar_t));
        if ((status = NvAPI_DRS_FindProfileByName(h_session, profile.profileName, h_profile)) == NVAPI_PROFILE_NOT_FOUND) {
            log_info("nvapi", "creating driver profile");
            auto gpu_support = NVDRS_GPU_SUPPORT {};
            gpu_support.nvs = 1;
            profile.gpuSupport = gpu_support;
            if ((status = NvAPI_DRS_CreateProfile(h_session, &profile, h_profile)) != NVAPI_OK && status != NVAPI_PROFILE_NAME_IN_USE) {
                log_warning("nvapi", "could not create driver profile: {}", get_error_message(status));
                return status;
            }
        } else if (status != NVAPI_OK) {
            log_warning("nvapi", "error finding driver profile: {}", get_error_message(status));
            return status;
        }

        log_info("nvapi", "creating application profile");
#ifdef SPICE64
        memcpy_s(application.appName, sizeof(application.appName), L"spice64.exe", 12 * sizeof(wchar_t));
        memcpy_s(application.userFriendlyName, sizeof(application.userFriendlyName), L"spice64", 8 * sizeof(wchar_t));
#else
        memcpy_s(application.appName, sizeof(application.appName), L"spice.exe", 10 * sizeof(wchar_t));
        memcpy_s(application.userFriendlyName, sizeof(application.userFriendlyName), L"spice", 6 * sizeof(wchar_t));
#endif
        if ((status = NvAPI_DRS_CreateApplication(h_session, *h_profile, &application)) != NVAPI_OK) {
            log_warning("nvapi", "could not create application profile: {}", get_error_message(status));
            return status;
        }

        return NVAPI_OK;
    }

    NvAPI_Status set_gpu_power_state(NvDRSSessionHandle h_session, NvDRSProfileHandle h_profile) {
        auto drs_setting = NVDRS_SETTING {};
        drs_setting.version = NVDRS_SETTING_VER;
        drs_setting.settingId = PREFERRED_PSTATE_ID;
        drs_setting.settingType = NVDRS_DWORD_TYPE;
        drs_setting.u32PredefinedValue = PREFERRED_PSTATE_PREFER_MAX;
        drs_setting.u32CurrentValue = PREFERRED_PSTATE_PREFER_MAX;
        return NvAPI_DRS_SetSetting(h_session, h_profile, &drs_setting);
    }

    NvAPI_Status set_gsync_mode(NvDRSSessionHandle h_session, NvDRSProfileHandle h_profile) {
        auto drs_setting = NVDRS_SETTING {};
        drs_setting.version = NVDRS_SETTING_VER;
        drs_setting.settingId = VRR_APP_OVERRIDE_ID;
        drs_setting.settingType = NVDRS_DWORD_TYPE;
        drs_setting.u32PredefinedValue = VRR_APP_OVERRIDE_FIXED_REFRESH;
        drs_setting.u32CurrentValue = VRR_APP_OVERRIDE_FIXED_REFRESH;
        return NvAPI_DRS_SetSetting(h_session, h_profile, &drs_setting);
    }

    NvAPI_Status set_vsync_mode(NvDRSSessionHandle h_session, NvDRSProfileHandle h_profile) {
        auto drs_setting = NVDRS_SETTING {};
        drs_setting.version = NVDRS_SETTING_VER;
        drs_setting.settingId = VSYNCMODE_ID;
        drs_setting.settingType = NVDRS_DWORD_TYPE;
        drs_setting.u32PredefinedValue = VSYNCMODE_PASSIVE;
        drs_setting.u32CurrentValue = VSYNCMODE_PASSIVE;
        return NvAPI_DRS_SetSetting(h_session, h_profile, &drs_setting);
    }

    NvAPI_Status set_refresh_rate(NvDRSSessionHandle h_session, NvDRSProfileHandle h_profile) {
        auto drs_setting = NVDRS_SETTING {};
        drs_setting.version = NVDRS_SETTING_VER;
        drs_setting.settingId = REFRESH_RATE_OVERRIDE_ID;
        drs_setting.settingType = NVDRS_DWORD_TYPE;
        drs_setting.u32PredefinedValue = REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED;
        drs_setting.u32CurrentValue = REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED;
        return NvAPI_DRS_SetSetting(h_session, h_profile, &drs_setting);
    }

    NvAPI_Status set_max_fps(NvDRSSessionHandle h_session, NvDRSProfileHandle h_profile) {
        auto drs_setting = NVDRS_SETTING {};
        drs_setting.version = NVDRS_SETTING_VER;
        drs_setting.settingId = FRL_FPS_ID;
        drs_setting.settingType = NVDRS_DWORD_TYPE;
        drs_setting.u32PredefinedValue = FRL_FPS_DISABLED;
        drs_setting.u32CurrentValue = FRL_FPS_DISABLED;
        return NvAPI_DRS_SetSetting(h_session, h_profile, &drs_setting);
    }

    void set_profile_settings() {
        if (!NVAPI_MODULE_LOAD_SUCCEEDED) {
            return;
        }

        NvDRSSessionHandle h_session = nullptr;
        log_info("nvapi", "creating driver settings session (DRS)...");
        auto status = NvAPI_DRS_CreateSession(&h_session);
        if (status != NVAPI_OK) {
            log_warning("nvapi", "could not create driver settings session: {}", get_error_message(status));
            return;
        }

        log_info("nvapi", "loading driver settings...");
        if ((status = NvAPI_DRS_LoadSettings(h_session)) != NVAPI_OK) {
            log_warning("nvapi", "could not load driver settings: {}", get_error_message(status));
            NvAPI_DRS_DestroySession(h_session);
            return;
        }

        NvDRSProfileHandle h_profile = nullptr;
        log_info("nvapi", "creating NVIDIA profile for spice(64).exe...");
        if (create_profile_if_needed(h_session, &h_profile) != NVAPI_OK) {
            NvAPI_DRS_DestroySession(h_session);
            return;
        }

        log_info("nvapi", "setting VSync to Application Controlled...");
        if ((status = set_vsync_mode(h_session, h_profile)) != NVAPI_OK) {
            log_warning("nvapi", "could not set VSync mode: {}", get_error_message(status));
        }

        log_info("nvapi", "applying preferred PState to Maximum Performance...");
        if ((status = set_gpu_power_state(h_session, h_profile)) != NVAPI_OK) {
            log_warning("nvapi", "could not set preferred PState: {}", get_error_message(status));
        }

        log_info("nvapi", "disabling G-SYNC...");
        if ((status = set_gsync_mode(h_session, h_profile)) != NVAPI_OK) {
            log_warning("nvapi", "could not set G-Sync mode: {}", get_error_message(status));
        }

        log_info("nvapi", "setting preferred refresh rate to Application Controlled...");
        if ((status = set_refresh_rate(h_session, h_profile)) != NVAPI_OK) {
            log_warning("nvapi", "could not set preferred refresh rate: {}", get_error_message(status));
        }

        log_info("nvapi", "disabling max framerate...");
        if ((status = set_max_fps(h_session, h_profile)) != NVAPI_OK) {
            log_warning("nvapi", "could not disable max framerate: {}", get_error_message(status));
        }

        log_info("nvapi", "saving settings for DRS session...");
        if ((status = NvAPI_DRS_SaveSettings(h_session)) != NVAPI_OK) {
            log_warning("nvapi", "could not save driver settings: {}", get_error_message(status));
            NvAPI_DRS_DestroySession(h_session);
            return;
        }

        log_info("nvapi", "destroying DRS session...");
        if ((status = NvAPI_DRS_DestroySession(h_session)) != NVAPI_OK) {
            log_warning("nvapi", "failed to destroy driver session: {}", get_error_message(status));
        }
    }

    void unload() {
        NVAPI_MODULE_LOAD_SUCCEEDED = false;
        if (NvAPI_Unload) {
            log_info("nvapi", "unloading NVAPI");
            NvAPI_Unload();
            NvAPI_Unload = nullptr;
        }

        if (nvapi) {
            FreeLibrary(nvapi);
            nvapi = nullptr;
        }
    }
}
