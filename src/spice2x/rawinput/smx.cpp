#include "smx.h"

using namespace std;

static HINSTANCE LIBSMX_INSTANCE = nullptr;
static string LIBSMX_NAME = "SMX.dll";

// SMX_Start API
static string SMX_StartStr = "SMX_Start";
typedef VOID(__stdcall* SMX_Start_t)(
    SMXUpdateCallback UpdateCallback, void *pUser
);
static SMX_Start_t pSMX_Start = nullptr;

// SMX_SetLights2 API
static string SMX_SetLights2Str = "SMX_SetLights2";
typedef VOID(__stdcall* SMX_SetLights2_t)(
    const char *lightData, int lightDataSize
);
static SMX_SetLights2_t pSMX_SetLights2 = nullptr;

// SMX_SetDedicatedCabinetLights API
static string SMX_SetDedicatedCabinetLightsStr = "SMX_SetDedicatedCabinetLights";
typedef VOID(__stdcall* SMX_SetDedicatedCabinetLights_t)(
    SMXDedicatedCabinetLights lightDevice, const char* lightData, int lightDataSize
);
static SMX_SetDedicatedCabinetLights_t pSMX_SetDedicatedCabinetLights = nullptr;

SMXWrapper::SMXWrapper() {
    static bool functions_loaded = false;

    if (functions_loaded)
        return;

    // See if we even have the SMX.dll available.
    LIBSMX_INSTANCE = libutils::try_library(LIBSMX_NAME);
    
    if (LIBSMX_INSTANCE != nullptr) {
        // Load functions
        pSMX_Start =
            (SMX_Start_t) libutils::get_proc(LIBSMX_INSTANCE, SMX_StartStr.c_str());
        pSMX_SetLights2 =
            (SMX_SetLights2_t) libutils::get_proc(LIBSMX_INSTANCE, SMX_SetLights2Str.c_str());
        pSMX_SetDedicatedCabinetLights =
            (SMX_SetDedicatedCabinetLights_t) libutils::get_proc(LIBSMX_INSTANCE, SMX_SetDedicatedCabinetLightsStr.c_str());

        // Make sure they actually loaded. We won't block on pSMX_SetDedicatedCabinetLights being null, 
        // since that functionality doesn't exist in the mainline SDK.
        if (pSMX_Start == nullptr || pSMX_SetLights2 == nullptr) {
            log_warning("smx", "Unable to load all external functions successfully");
        } else {
            functions_loaded = true;
        }

        // This only exists in a fork of the SMX SDK right now, don't consider the functions not
        // to be loaded if this is null
        if (pSMX_SetDedicatedCabinetLights == nullptr) {
            log_warning("smx", "Unable to load cabinet lights functionality");
        }
    } else {
        log_warning("smx", "Could not load \'{}\'", LIBSMX_NAME);
    }
}

SMXWrapper::~SMXWrapper() {}

void SMXWrapper::SMX_Start(SMXUpdateCallback UpdateCallback, void *pUser) {
    if (pSMX_Start != nullptr) {
        pSMX_Start(UpdateCallback, pUser);
    }
}

void SMXWrapper::SMX_SetLights2(const char *lightData, int lightDataSize) {
    if (pSMX_SetLights2 != nullptr) {
        pSMX_SetLights2(lightData, lightDataSize);
    }
}

void SMXWrapper::SMX_SetDedicatedCabinetLights(SMXDedicatedCabinetLights lightDevice, const char *lightData, int lightDataSize) {
    if (pSMX_SetDedicatedCabinetLights != nullptr) {
        pSMX_SetDedicatedCabinetLights(lightDevice, lightData, lightDataSize);
    }
}
