#include "external/nvapi/nvapi.h"
#include "external/nvapi/NvApiDriverSettings.h"
#include "avs/game.h"
#include "hooks/libraryhook.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

#include "nvapi_hook.h"

namespace nvapi_hook {

    bool BYPASS_NVAPI = false;

    typedef uintptr_t *(*NvAPI_QueryInterface_t)(unsigned int);

    static NvAPI_QueryInterface_t NvAPI_QueryInterface_orig = nullptr;
    static decltype(NvAPI_GPU_GetConnectedDisplayIds) *NvAPI_GPU_GetConnectedDisplayIds_orig = nullptr;
    static decltype(NvAPI_DISP_GetGDIPrimaryDisplayId) *NvAPI_DISP_GetGDIPrimaryDisplayId_orig = nullptr;

    static uintptr_t* __cdecl NvAPI_QueryInterface_hook(unsigned int func_code);
    static NvAPI_Status __cdecl NvAPI_GPU_GetConnectedDisplayIds_hook(
        NvPhysicalGpuHandle hPhysicalGpu,
        NV_GPU_DISPLAYIDS *pDisplayIds,
        NvU32 *pDisplayIdCount,
        NvU32 flags);
    static NvAPI_Status __cdecl NvAPI_DISP_SetDisplayConfig_hook(
        NvU32 pathInfoCount, NV_DISPLAYCONFIG_PATH_INFO *pathInfo, NvU32 flags);

    void initialize(HINSTANCE dll) {
#ifdef SPICE64
        std::string nvapi_dll = "nvapi64.dll";
#else
        std::string nvapi_dll = "nvapi.dll";
#endif

        detour::trampoline_try(
            nvapi_dll.c_str(), "nvapi_QueryInterface",
            NvAPI_QueryInterface_hook, &NvAPI_QueryInterface_orig);
    }

    uintptr_t* __cdecl NvAPI_QueryInterface_hook(unsigned int func_code) {
        if (BYPASS_NVAPI) {
            log_misc(
                "nvapi_hook",
                "NvAPI_QueryInterface(0x{:x}) - block all calls to nvapi (-nonvapi)",
                func_code);
            return nullptr;
        }

        // spoof display connector types only for SDVX
        if (func_code == 0x0078DBA2 && avs::game::is_model("KFC")) {
            NvAPI_GPU_GetConnectedDisplayIds_orig =
                reinterpret_cast<decltype(NvAPI_GPU_GetConnectedDisplayIds) *>(
                    NvAPI_QueryInterface_orig(func_code));
            NvAPI_DISP_GetGDIPrimaryDisplayId_orig =
                reinterpret_cast<decltype(NvAPI_DISP_GetGDIPrimaryDisplayId) *>(
                    NvAPI_QueryInterface_orig(0x1E9D8A31));
            log_misc("nvapi_hook", "NvAPI_QueryInterface(NvAPI_GPU_GetConnectedDisplayIds) - hooked");
            return (uintptr_t *)NvAPI_GPU_GetConnectedDisplayIds_hook;
        }

        // NvAPI_DISP_SetDisplayConfig
        if (func_code == 0x5D8CF8DE) { 
            log_misc("nvapi_hook", "NvAPI_QueryInterface(NvAPI_DISP_SetDisplayConfig) - hooked");
            return (uintptr_t *)NvAPI_DISP_SetDisplayConfig_hook;
        }
        
        // all others: let the game call nvapi directly
        log_misc("nvapi_hook", "NvAPI_QueryInterface(0x{:x}) - pass through to nvapi", func_code);
        return NvAPI_QueryInterface_orig(func_code);
    }

    NvAPI_Status __cdecl NvAPI_GPU_GetConnectedDisplayIds_hook(
            NvPhysicalGpuHandle hPhysicalGpu,
            NV_GPU_DISPLAYIDS *pDisplayIds,
            NvU32 *pDisplayIdCount,
            NvU32 flags) {

        if (NvAPI_GPU_GetConnectedDisplayIds_orig == nullptr) {
            return NVAPI_NO_IMPLEMENTATION;
        }

        const NvAPI_Status status = NvAPI_GPU_GetConnectedDisplayIds_orig(
            hPhysicalGpu,
            pDisplayIds,
            pDisplayIdCount,
            flags);

        if (status != NVAPI_OK || pDisplayIds == nullptr || pDisplayIdCount == nullptr ||
            NvAPI_DISP_GetGDIPrimaryDisplayId_orig == nullptr) {
            return status;
        }

        NvU32 primary_display_id = 0;
        if (NvAPI_DISP_GetGDIPrimaryDisplayId_orig(&primary_display_id) != NVAPI_OK) {
            return status;
        }

        // report the primary display adapter as DP and all other displays as HDMI
        // (the game expects this since the VM cab is like that)
        //
        // this doesn't actually affect gameplay; only how
        //    NvDisplayConfig: MainDisplay={}hz / SubDisplay={}hz
        // gets printed.
        //
        // for the actual value that matters for gameplay, it's this:
        //     NvDisplayConfig: GetMainDisplayRefreshRate = {}hz
        for (NvU32 index = 0; index < *pDisplayIdCount; index++) {
            auto &display = pDisplayIds[index];
            display.connectorType = display.displayId == primary_display_id ?
                NV_MONITOR_CONN_TYPE_DP : NV_MONITOR_CONN_TYPE_HDMI;
        }

        return status;
    }

    NvAPI_Status __cdecl NvAPI_DISP_SetDisplayConfig_hook(
        NvU32 pathInfoCount, NV_DISPLAYCONFIG_PATH_INFO *pathInfo, NvU32 flags) {
        // do not let the game apply its emulated cabinet topology to Windows
        log_misc("nvapi_hook", "NvAPI_DISP_SetDisplayConfig_hook - do nothing and return");
        return NVAPI_OK;
    }
}
