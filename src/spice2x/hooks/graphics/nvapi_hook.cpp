#include "external/nvapi/nvapi.h"
#include "external/nvapi/NvApiDriverSettings.h"
#include "hooks/libraryhook.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

#include "nvapi_hook.h"

namespace nvapi_hook {

    bool BYPASS_NVAPI = false;

    typedef uintptr_t *(*NvAPI_QueryInterface_t)(unsigned int);
    static NvAPI_QueryInterface_t NvAPI_QueryInterface_orig = nullptr;

    static uintptr_t* __cdecl NvAPI_QueryInterface_hook(unsigned int func_code);
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

        // NvAPI_DISP_SetDisplayConfig
        if (func_code == 0x5D8CF8DE) { 
            log_misc("nvapi_hook", "NvAPI_QueryInterface(NvAPI_DISP_SetDisplayConfig) - hooked");
            return (uintptr_t *)NvAPI_DISP_SetDisplayConfig_hook;
        }
        
        // all others: let the game call nvapi directly
        log_misc("nvapi_hook", "NvAPI_QueryInterface(0x{:x}) - pass through to nvapi", func_code);
        return NvAPI_QueryInterface_orig(func_code);
    }

    NvAPI_Status __cdecl NvAPI_DISP_SetDisplayConfig_hook(
        NvU32 pathInfoCount, NV_DISPLAYCONFIG_PATH_INFO *pathInfo, NvU32 flags) {
        log_misc("nvapi_hook", "NvAPI_DISP_SetDisplayConfig_hook - do nothing and return");
        return NVAPI_OK;
    }
}
