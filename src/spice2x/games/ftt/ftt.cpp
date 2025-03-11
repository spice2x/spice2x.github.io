#include "ftt.h"
#include "nui.h"

#include "launcher/launcher.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/sigscan.h"
#include "util/memutils.h"

typedef HRESULT (WINAPI *NuiGetSensorCount_t)(int *pCount);
typedef HRESULT (WINAPI *NuiCreateSensorByIndex_t)(int index, INuiSensor **ppNuiSensor);

static NuiGetSensorCount_t NuiGetSensorCount_orig = nullptr;
static NuiCreateSensorByIndex_t NuiCreateSensorByIndex_orig = nullptr;

static VTBL_TYPE(INuiSensor, NuiInitialize) INuiSensor_NuiInitialize_orig = nullptr;
static VTBL_TYPE(INuiSensor, NuiImageStreamOpen) INuiSensor_NuiImageStreamOpen_orig = nullptr;

static HRESULT __stdcall INuiSensor_NuiInitialize_hook(INuiSensor *This, DWORD dwFlags) {
    log_misc("ftt", "INuiSensor::NuiInitialize hook hit (dwFlags: {})", dwFlags);

    // call original function
    HRESULT ret = INuiSensor_NuiInitialize_orig(This, dwFlags);

    // check return value
    if (FAILED(ret)) {
        log_warning("ftt", "INuiSensor::NuiInitialize failed, hr={}", FMT_HRESULT(ret));
    }

    return ret;
}

static HRESULT __stdcall INuiSensor_NuiImageStreamOpen_hook(
    INuiSensor *This,
    NUI_IMAGE_TYPE eImageType,
    NUI_IMAGE_RESOLUTION eResolution,
    DWORD dwImageFrameFlags,
    DWORD dwFrameLimit,
    HANDLE hNextFrameEvent,
    HANDLE *phStreamHandle) {
    log_misc("ftt", "INuiSensor::NuiImageStreamOpen hook hit");

    // fix unsupported flag
    if (eImageType == NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX) {
        dwImageFrameFlags = 0;
    }

    // call original function
    HRESULT ret = INuiSensor_NuiImageStreamOpen_orig(This, eImageType, eResolution, dwImageFrameFlags, dwFrameLimit, hNextFrameEvent, phStreamHandle);

    // check return value
    if (FAILED(ret)) {
        log_warning("ftt", "INuiSensor::NuiImageStreamOpen failed, hr={}", FMT_HRESULT(ret));
    }

    return ret;
}

static HRESULT WINAPI NuiGetSensorCount_hook(int *pCount) {
    log_misc("ftt", "NuiGetSensorCount hook hit");

    // call original function
    HRESULT ret = NuiGetSensorCount_orig(pCount);

    // check return value
    if (FAILED(ret)) {
        log_warning("ftt", "NuiGetSensorCount failed, hr={}", FMT_HRESULT(ret));
        return ret;
    }

    if (pCount == nullptr) {
        return ret;
    }

    log_info("ftt", "found {} Kinect sensors", *pCount);

    return ret;
}

static HRESULT WINAPI NuiCreateSensorByIndex_hook(int index, INuiSensor **ppNuiSensor) {
    log_misc("ftt", "NuiCreateSensorByIndex hook hit");

    // call original function
    HRESULT ret = NuiCreateSensorByIndex_orig(index, ppNuiSensor);

    // check return value
    if (FAILED(ret)) {
        log_warning("ftt", "NuiCreateSensorByIndex failed, hr={}", FMT_HRESULT(ret));
        return ret;
    }
    if (ppNuiSensor == nullptr) {
        return ret;
    }

    // save original functions
    INuiSensor *pNuiSensor = *ppNuiSensor;
    if (INuiSensor_NuiInitialize_orig == nullptr) {
        INuiSensor_NuiInitialize_orig = pNuiSensor->lpVtbl->NuiInitialize;
    }
    if (INuiSensor_NuiImageStreamOpen_orig == nullptr) {
        INuiSensor_NuiImageStreamOpen_orig = pNuiSensor->lpVtbl->NuiImageStreamOpen;
    }

    // unlock interface
    memutils::VProtectGuard pNuiSensor_guard(pNuiSensor->lpVtbl);

    // hook interface
    pNuiSensor->lpVtbl->NuiInitialize = INuiSensor_NuiInitialize_hook;
    pNuiSensor->lpVtbl->NuiImageStreamOpen = INuiSensor_NuiImageStreamOpen_hook;

    return ret;
}

namespace games::ftt {

    FTTGame::FTTGame() : Game("FutureTomTom") {
    }

    void FTTGame::attach() {
        Game::attach();

        // load main game module so patches work
        auto game_dll = libutils::load_library(MODULE_PATH / "gamemmd.dll");

        // patch kinect to always poll
        if (!replace_pattern(game_dll,
                "78??837B340775??488BCBE8",
                "????????????9090????????",
                0,
                0))
        {
            log_warning("ftt", "kinect polling patch failed");
        }

        // patch kinect to not time out
        if (!replace_pattern(game_dll,
                "3D3075000072??C74334060000004883C420",
                "??????????????90909090909090????????",
                0,
                0))
        {
            log_warning("ftt", "kinect timeout patch failed");
        }

        // hook Kinect calls
        NuiGetSensorCount_orig = (NuiGetSensorCount_t) detour::iat_ordinal(
                "Kinect10.dll", 5, (void *) NuiGetSensorCount_hook, game_dll);
        NuiCreateSensorByIndex_orig = (NuiCreateSensorByIndex_t) detour::iat_ordinal(
                "Kinect10.dll", 6, (void *) NuiCreateSensorByIndex_hook, game_dll);
    }
}
