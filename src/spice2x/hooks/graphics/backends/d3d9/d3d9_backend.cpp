#include "d3d9_backend.h"

#include <cassert>
#include <memory>
#include <thread>
#include <vector>
#include <external/robin_hood.h>

#include <d3d9.h>
#ifdef __GNUC__
#include <d3dx9tex.h>
#endif

#include "avs/game.h"
#include "cfg/screen_resize.h"
#include "games/iidx/iidx.h"
#include "games/sdvx/sdvx.h"
#include "games/io.h"
#include "hooks/graphics/graphics.h"
#include "launcher/launcher.h"
#include "launcher/options.h"
#include "launcher/shutdown.h"
#include "misc/clipboard.h"
#include "misc/eamuse.h"
#include "misc/wintouchemu.h"
#include "overlay/overlay.h"
#include "util/detour.h"
#include "util/flags_helper.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/memutils.h"
#include "util/threadpool.h"

#include "d3d9_device.h"

#ifdef min
#undef min
#endif

#define CHECK_RESULT(x) \
    do { \
        HRESULT __ret = (x); \
        if (GRAPHICS_LOG_HRESULT && FAILED(__ret)) [[unlikely]] { \
            log_warning("graphics::d3d9", "{} failed, hr={}", __FUNCTION__, FMT_HRESULT(__ret)); \
        } \
        return __ret; \
    } while (0)

#ifdef __GNUC__
typedef decltype(D3DXSaveSurfaceToFileA) *D3DXSaveSurfaceToFileA_t;
#else
#define D3DXIFF_PNG ((DWORD) 3)

typedef HRESULT (WINAPI *D3DXSaveSurfaceToFileA_t)(
        LPCSTR pDestFile,
        DWORD DestFormat,
        LPDIRECT3DSURFACE9 pSrcSurface,
        CONST PALETTEENTRY *pSrcPalette,
        CONST RECT *pSrcRect);
#endif

/*
 * 9 on 12
 */

#define MAX_D3D9ON12_QUEUES 2
typedef struct _D3D9ON12_ARGS {
    BOOL Enable9On12;
    IUnknown *pD3D12Device;
    IUnknown *ppD3D12Queues[MAX_D3D9ON12_QUEUES];
    UINT NumQueues;
    UINT NodeMask;
} D3D9ON12_ARGS;

typedef HRESULT (WINAPI *Direct3DCreate9On12Ex_t)(
        UINT SDKVersion, D3D9ON12_ARGS *pOverrideList,
        UINT NumOverrideEntries, IDirect3D9Ex** ppOutputInterface);
typedef IDirect3D9* (WINAPI *Direct3DCreate9On12_t)(
        UINT SDKVersion, D3D9ON12_ARGS *pOverrideList, UINT NumOverrideEntries);

// state
static void *D3D9_DIRECT3D_CREATE9_ADR = nullptr;
static char D3D9_DIRECT3D_CREATE9_CONTENTS[16];

static bool ATTEMPTED_D3DX9_LOAD_LIBRARY = false;

// settings
std::optional<UINT> D3D9_ADAPTER = std::nullopt;
DWORD D3D9_BEHAVIOR_DISABLE = 0;

static decltype(Direct3DCreate9) *Direct3DCreate9_orig = nullptr;
static decltype(Direct3DCreate9Ex) *Direct3DCreate9Ex_orig = nullptr;
static Direct3DCreate9On12_t Direct3DCreate9On12_orig = nullptr;
static Direct3DCreate9On12Ex_t Direct3DCreate9On12Ex_orig = nullptr;

static bool ATTEMPTED_SUB_SWAP_CHAIN_ACQUIRE = false;
static IDirect3DSwapChain9 *SUB_SWAP_CHAIN = nullptr;

static void graphics_d3d9_ldj_init_sub_screen(IDirect3DDevice9Ex *device, D3DPRESENT_PARAMETERS *present_params);

static std::string behavior2s(DWORD behavior_flags) {
    FLAGS_START(behavior_flags);
    FLAG(behavior_flags, D3DCREATE_FPU_PRESERVE);
    FLAG(behavior_flags, D3DCREATE_MULTITHREADED);
    FLAG(behavior_flags, D3DCREATE_PUREDEVICE);
    FLAG(behavior_flags, D3DCREATE_SOFTWARE_VERTEXPROCESSING);
    FLAG(behavior_flags, D3DCREATE_HARDWARE_VERTEXPROCESSING);
    FLAG(behavior_flags, D3DCREATE_MIXED_VERTEXPROCESSING);
    FLAG(behavior_flags, D3DCREATE_DISABLE_DRIVER_MANAGEMENT);
    FLAG(behavior_flags, D3DCREATE_ADAPTERGROUP_DEVICE);
    FLAG(behavior_flags, D3DCREATE_DISABLE_DRIVER_MANAGEMENT_EX);
    FLAG(behavior_flags, D3DCREATE_NOWINDOWCHANGES);
    FLAG(behavior_flags, D3DCREATE_DISABLE_PSGP_THREADING);
    FLAG(behavior_flags, D3DCREATE_ENABLE_PRESENTSTATS);
    FLAG(behavior_flags, D3DCREATE_DISABLE_PRINTSCREEN);
    FLAG(behavior_flags, D3DCREATE_SCREENSAVER);
    FLAGS_END(behavior_flags);
}

static std::string format2s(D3DFORMAT format) {
    switch (format) {
        ENUM_VARIANT(D3DFMT_UNKNOWN);
        ENUM_VARIANT(D3DFMT_R8G8B8);
        ENUM_VARIANT(D3DFMT_A8R8G8B8);
        ENUM_VARIANT(D3DFMT_X8R8G8B8);
        ENUM_VARIANT(D3DFMT_R5G6B5);
        ENUM_VARIANT(D3DFMT_X1R5G5B5);
        ENUM_VARIANT(D3DFMT_A1R5G5B5);
        ENUM_VARIANT(D3DFMT_A4R4G4B4);
        ENUM_VARIANT(D3DFMT_R3G3B2);
        ENUM_VARIANT(D3DFMT_A8);
        ENUM_VARIANT(D3DFMT_A8R3G3B2);
        ENUM_VARIANT(D3DFMT_X4R4G4B4);
        ENUM_VARIANT(D3DFMT_A2B10G10R10);
        ENUM_VARIANT(D3DFMT_A8B8G8R8);
        ENUM_VARIANT(D3DFMT_X8B8G8R8);
        ENUM_VARIANT(D3DFMT_G16R16);
        ENUM_VARIANT(D3DFMT_A2R10G10B10);
        ENUM_VARIANT(D3DFMT_A16B16G16R16);
        ENUM_VARIANT(D3DFMT_A8P8);
        ENUM_VARIANT(D3DFMT_P8);
        ENUM_VARIANT(D3DFMT_L8);
        ENUM_VARIANT(D3DFMT_A8L8);
        ENUM_VARIANT(D3DFMT_A4L4);
        ENUM_VARIANT(D3DFMT_V8U8);
        ENUM_VARIANT(D3DFMT_L6V5U5);
        ENUM_VARIANT(D3DFMT_X8L8V8U8);
        ENUM_VARIANT(D3DFMT_Q8W8V8U8);
        ENUM_VARIANT(D3DFMT_V16U16);
        ENUM_VARIANT(D3DFMT_A2W10V10U10);
        ENUM_VARIANT(D3DFMT_UYVY);
        ENUM_VARIANT(D3DFMT_YUY2);
        ENUM_VARIANT(D3DFMT_DXT1);
        ENUM_VARIANT(D3DFMT_DXT2);
        ENUM_VARIANT(D3DFMT_DXT3);
        ENUM_VARIANT(D3DFMT_DXT4);
        ENUM_VARIANT(D3DFMT_DXT5);
        ENUM_VARIANT(D3DFMT_MULTI2_ARGB8);
        ENUM_VARIANT(D3DFMT_G8R8_G8B8);
        ENUM_VARIANT(D3DFMT_R8G8_B8G8);
        ENUM_VARIANT(D3DFMT_D16_LOCKABLE);
        ENUM_VARIANT(D3DFMT_D32);
        ENUM_VARIANT(D3DFMT_D15S1);
        ENUM_VARIANT(D3DFMT_D24S8);
        ENUM_VARIANT(D3DFMT_D24X8);
        ENUM_VARIANT(D3DFMT_D24X4S4);
        ENUM_VARIANT(D3DFMT_D16);
        ENUM_VARIANT(D3DFMT_L16);
        ENUM_VARIANT(D3DFMT_D32F_LOCKABLE);
        ENUM_VARIANT(D3DFMT_D24FS8);
        ENUM_VARIANT(D3DFMT_D32_LOCKABLE);
        ENUM_VARIANT(D3DFMT_S8_LOCKABLE);
        ENUM_VARIANT(D3DFMT_VERTEXDATA);
        ENUM_VARIANT(D3DFMT_INDEX16);
        ENUM_VARIANT(D3DFMT_INDEX32);
        ENUM_VARIANT(D3DFMT_Q16W16V16U16);
        ENUM_VARIANT(D3DFMT_R16F);
        ENUM_VARIANT(D3DFMT_G16R16F);
        ENUM_VARIANT(D3DFMT_A16B16G16R16F);
        ENUM_VARIANT(D3DFMT_R32F);
        ENUM_VARIANT(D3DFMT_G32R32F);
        ENUM_VARIANT(D3DFMT_A32B32G32R32F);
        ENUM_VARIANT(D3DFMT_CxV8U8);
        ENUM_VARIANT(D3DFMT_A1);
        ENUM_VARIANT(D3DFMT_A2B10G10R10_XR_BIAS);
        ENUM_VARIANT(D3DFMT_BINARYBUFFER);
        default:
            return fmt::to_string(format);
    }
}

static std::string presentation_interval2s(UINT presentation_interval) {
    FLAGS_START(presentation_interval);
    FLAG(presentation_interval, D3DPRESENT_INTERVAL_DEFAULT);
    FLAG(presentation_interval, D3DPRESENT_INTERVAL_ONE);
    FLAG(presentation_interval, D3DPRESENT_INTERVAL_TWO);
    FLAG(presentation_interval, D3DPRESENT_INTERVAL_THREE);
    FLAG(presentation_interval, D3DPRESENT_INTERVAL_FOUR);
    FLAG(presentation_interval, D3DPRESENT_INTERVAL_IMMEDIATE);
    FLAGS_END(presentation_interval);
}

static void update_backbuffer_dimensions(D3DPRESENT_PARAMETERS *params);

static bool is_dx9_on_12_enabled() {
    switch (GRAPHICS_9_ON_12_STATE) {
        case DX9ON12_FORCE_OFF:
            log_info("graphics::d3d9", "DirectX 9on12: forced OFF by user (-dx9on12)");
            return false;

        case DX9ON12_FORCE_ON:
            log_info("graphics::d3d9", "DirectX 9on12: forced ON by user (-9on12 or -dx9on12)");
            return true;

        case DX9ON12_AUTO:
        default:
            if (GRAPHICS_9_ON_12_REQUESTED_BY_GAME) {
                log_info(
                    "graphics::d3d9",
                    "DirectX 9on12: enabled automatically for current game (tip: use -dx9on12 to force on or off)");
                return true;
            } else {
                log_info(
                    "graphics::d3d9",
                    "DirectX 9on12: disabled by default, using DX9 (tip: use -dx9on12 to force on or off)");
                return false;
            }
    }
}

static IDirect3D9 *WINAPI Direct3DCreate9_hook(UINT SDKVersion) {
    log_misc("graphics::d3d9", "Direct3DCreate9 hook hit");

    // remove hook
    if (D3D9_DIRECT3D_CREATE9_ADR) {
        detour::inline_restore(D3D9_DIRECT3D_CREATE9_ADR, D3D9_DIRECT3D_CREATE9_CONTENTS);

        if (Direct3DCreate9_orig == nullptr) {
            Direct3DCreate9_orig = reinterpret_cast<decltype(Direct3DCreate9) *>(D3D9_DIRECT3D_CREATE9_ADR);
        }
    }

    // create interface
    IDirect3D9 *value;
    if (is_dx9_on_12_enabled()) {
        if (!Direct3DCreate9On12_orig) {
            log_fatal("graphics::d3d9", "unable to find Direct3DCreate9On12");
        } else {
            D3D9ON12_ARGS args = {};
            args.Enable9On12 = TRUE;
            value = Direct3DCreate9On12_orig(SDKVersion, &args, 1);
        }
    } else {
        value = Direct3DCreate9_orig(SDKVersion);
    }
    if (value == nullptr) {
        log_warning("graphics::d3d9", "failed to create Direct3D interface for {}", SDKVersion);

        return value;
    }

    value = new WrappedIDirect3D9(value);

    // add hook
    if (D3D9_DIRECT3D_CREATE9_ADR) {
        detour::inline_noprotect((void *) Direct3DCreate9_hook, D3D9_DIRECT3D_CREATE9_ADR);
    }

    // return modified interface
    return value;
}

static HRESULT WINAPI Direct3DCreate9Ex_hook(UINT SDKVersion, IDirect3D9Ex **d3d9ex) {
    log_misc("graphics::d3d9", "Direct3DCreate9Ex hook hit");

    // call original
    HRESULT result;
    if (is_dx9_on_12_enabled()) {
        if (!Direct3DCreate9On12Ex_orig) {
            log_fatal("graphics::d3d9", "unable to find Direct3DCreate9On12Ex");
        } else {
            D3D9ON12_ARGS args = {};
            args.Enable9On12 = TRUE;
            result = Direct3DCreate9On12Ex_orig(SDKVersion, &args, 1, d3d9ex);
        }
    } else {
        result = Direct3DCreate9Ex_orig(SDKVersion, d3d9ex);
    }
    if (FAILED(result) || *d3d9ex == nullptr) {
        log_warning("graphics::d3d9", "failed to create Direct3D interface for {}, hr={}",
                SDKVersion,
                FMT_HRESULT(result));

        return result;
    }

    *d3d9ex = new WrappedIDirect3D9(*d3d9ex);

    // return original result
    return result;
}

void graphics_d3d9_init() {
    log_info("graphics::d3d9", "initializing");

    // DX9 inline hooks
    HMODULE d3d9 = libutils::try_module("d3d9.dll");
    if (!d3d9) {
        log_info("graphics::d3d9", "skipping inline hooks");
    } else {

        // 9 on 12
        Direct3DCreate9On12_orig = (Direct3DCreate9On12_t) libutils::try_proc(d3d9, "Direct3DCreate9On12");
        Direct3DCreate9On12Ex_orig = (Direct3DCreate9On12Ex_t) libutils::try_proc(d3d9, "Direct3DCreate9On12Ex");

        // inline hooks
        D3D9_DIRECT3D_CREATE9_ADR = (void *) libutils::get_proc(d3d9, "Direct3DCreate9");
        detour::inline_preserve(
                reinterpret_cast<void *>(Direct3DCreate9_hook),
                D3D9_DIRECT3D_CREATE9_ADR,
                D3D9_DIRECT3D_CREATE9_CONTENTS);
    }

    // DX9 IAT hooks
    Direct3DCreate9_orig = detour::iat_try("Direct3DCreate9", Direct3DCreate9_hook);
    Direct3DCreate9Ex_orig = detour::iat_try("Direct3DCreate9Ex", Direct3DCreate9Ex_hook);
}

/*
 * IUnknown
 */

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::QueryInterface(
        REFIID riid,
        void **ppvObj)
{
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIDirect3D9 ||
        riid == IID_IDirect3D9 ||
        riid == IID_IDirect3D9Ex)
    {
        // update to IDirect3DDevice9Ex interface
        if (!is_d3d9ex && riid == IID_IDirect3D9Ex) {
            IDirect3D9Ex *ex = nullptr;

            HRESULT ret = pReal->QueryInterface(IID_PPV_ARGS(&ex));
            if (FAILED(ret) || ex == nullptr) {
                if (ret != E_NOINTERFACE) {
                    log_warning("graphics::d3d9",
                            "failed to upgrade to IDirect3DDevice9Ex, hr={}",
                            FMT_HRESULT(ret));
                }

                return E_NOINTERFACE;
            }

            pReal->Release();
            pReal = ex;
            is_d3d9ex = true;
        }

        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3D9::AddRef() {
    return pReal->AddRef();
}

ULONG STDMETHODCALLTYPE WrappedIDirect3D9::Release() {
    ULONG refs = this->pReal != nullptr ? this->pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

/*
 * IDirect3D9
 */

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::RegisterSoftwareDevice(void *pInitializeFunction) {
    CHECK_RESULT(pReal->RegisterSoftwareDevice(pInitializeFunction));
}

UINT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterCount() {
    return pReal->GetAdapterCount();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterIdentifier(
        UINT Adapter,
        DWORD Flags,
        D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
    CHECK_RESULT(pReal->GetAdapterIdentifier(Adapter, Flags, pIdentifier));
}

UINT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) {
    return pReal->GetAdapterModeCount(Adapter, Format);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::EnumAdapterModes(
        UINT Adapter,
        D3DFORMAT Format,
        UINT Mode,
        D3DDISPLAYMODE *pMode)
{
    HRESULT ret = pReal->EnumAdapterModes(Adapter, Format, Mode, pMode);

    if (SUCCEEDED(ret) && pMode) {
        /*
        log_misc("graphics::d3d9", "IDirect3D9::EnumAdapterMode({}, {}, {}) => {}x{} @ {} Hz ({})",
                Adapter,
                format2s(Format),
                Mode,
                pMode->Width,
                pMode->Height,
                pMode->RefreshRate,
                format2s(pMode->Format));
                */

        bool modified = false;
        auto width = pMode->Width;
        auto height = pMode->Height;
        auto refresh = pMode->RefreshRate;

        if (avs::game::is_model("LDJ")) {
            if (Mode == 0 && games::iidx::TDJ_MODE) {
                if (games::iidx::is_tdj_fhd()) {
                    log_misc("graphics::d3d9", "overriding mode 0 to 1920x1080 @ 120 Hz (for TDJ FHD)");
                    pMode->Width = 1920;
                    pMode->Height = 1080;
                    pMode->RefreshRate = 120;
                    modified = true;
                } else {
                    log_misc("graphics::d3d9", "overriding mode 0 to 1280x720 @ 120 Hz (for TDJ HD)");
                    pMode->Width = 1280;
                    pMode->Height = 720;
                    pMode->RefreshRate = 120;
                    modified = true;
                }
            }

            // For whatever reason, TDJ FHD mode prefers to pick lower (~60Hz) resolutions instead
            // of 1080p@120Hz. Remove them here and try to force 120+ Hz.
            if (!modified && games::iidx::is_tdj_fhd() && refresh < 110) {
                if ((width == 1920 && height == 1080) ||
                    (width == 1280 && height == 720)){
                    log_misc(
                        "graphics::d3d9", "removing mode {}, {}x{} @ {}Hz (for TDJ FHD)",
                        Mode, width, height, refresh);
                    memset(pMode, 0, sizeof(*pMode));
                    modified = true;
                }
            }

            // zero out display mode for bad entries
            // - skip non-native resolutions
            // - skip 75 and 90 Hz entries because LDJ game timing is messed up with it
            //   (TDJ should be fine as it assumes a fixed 60 Hz timing)
            if (!modified && (width == 1360 || width == 1366 || refresh == 75 || refresh == 90)) {
                log_misc(
                    "graphics::d3d9", "removing mode {}, {}x{} @ {}Hz (for LDJ/TDJ)",
                    Mode, width, height, refresh);
                memset(pMode, 0, sizeof(*pMode));
                modified = true;
            }

            if (!modified && !games::iidx::TDJ_MODE && games::iidx::FORCE_720P && (height > 720)) {
                log_misc(
                    "graphics::d3d9", "removing mode {}, {}x{} @ {}Hz (-iidxforce720p)",
                    Mode, width, height, refresh);
                memset(pMode, 0, sizeof(*pMode));
                modified = true;
            }
        }

        if (!modified &&
            (width > height) &&
            (GRAPHICS_ADJUST_ORIENTATION == ORIENTATION_CW ||
             GRAPHICS_ADJUST_ORIENTATION == ORIENTATION_CCW)) {
            log_misc(
                "graphics::d3d9", "swapping width and height for mode {}, {}x{} @ {}Hz (-autoorientation)",
                Mode, width, height, refresh);
            pMode->Height = width;
            pMode->Width = height;
            modified = true;
        }
    }

    CHECK_RESULT(ret);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) {
    CHECK_RESULT(pReal->GetAdapterDisplayMode(Adapter, pMode));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceType(
        UINT iAdapter,
        D3DDEVTYPE DevType,
        D3DFORMAT DisplayFormat,
        D3DFORMAT BackBufferFormat,
        BOOL bWindowed)
{
    CHECK_RESULT(pReal->CheckDeviceType(iAdapter, DevType, DisplayFormat, BackBufferFormat, bWindowed));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceFormat(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        D3DFORMAT AdapterFormat,
        DWORD Usage,
        D3DRESOURCETYPE RType,
        D3DFORMAT CheckFormat)
{
    CHECK_RESULT(pReal->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceMultiSampleType(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        D3DFORMAT SurfaceFormat,
        BOOL Windowed,
        D3DMULTISAMPLE_TYPE MultiSampleType,
        DWORD *pQualityLevels)
{
    CHECK_RESULT(pReal->CheckDeviceMultiSampleType(
            Adapter,
            DeviceType,
            SurfaceFormat,
            Windowed,
            MultiSampleType,
            pQualityLevels));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDepthStencilMatch(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        D3DFORMAT AdapterFormat,
        D3DFORMAT RenderTargetFormat,
        D3DFORMAT DepthStencilFormat)
{
    CHECK_RESULT(pReal->CheckDepthStencilMatch(
            Adapter,
            DeviceType,
            AdapterFormat,
            RenderTargetFormat,
            DepthStencilFormat));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceFormatConversion(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        D3DFORMAT SourceFormat,
        D3DFORMAT TargetFormat)
{
    CHECK_RESULT(pReal->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) {
    log_misc("graphics::d3d9", "IDirect3D9::GetDeviceCaps hook hit");

    if (!pCaps) {
        log_warning("graphics::d3d9", "NULL pointer passed in for required parameter");
        return D3DERR_INVALIDCALL;
    }

    // SDVX uses `NumberOfAdaptersInGroup` to allocate a vector and the Microsoft documentation states:
    // "The value will be 0 for a subordinate adapter of a multihead card. Each card can have at most one
    // master, but may have many subordinates." Therefore, this must point to the master adapter.
    if (Adapter == 0 && D3D9_ADAPTER.has_value()) {
        Adapter = D3D9_ADAPTER.value();

        // Get the master adapter ordinal
        HRESULT result = this->pReal->GetDeviceCaps(Adapter, DeviceType, pCaps);
        if (FAILED(result)) {
            log_warning("graphics::d3d9", "GetDeviceCaps failed, hr={}", FMT_HRESULT(result));
            return result;
        }

        // Now get the device caps for the master adapter
        Adapter = pCaps->MasterAdapterOrdinal;
    }

    HRESULT ret = this->pReal->GetDeviceCaps(Adapter, DeviceType, pCaps);
    if (FAILED(ret)) {
        return ret;
    }

    if (avs::game::is_model("LDJ")) {
        if (!GRAPHICS_WINDOWED) {
            // use 2 so that the subscreen overlay can be drawn in full screen with single monitor
            pCaps->NumberOfAdaptersInGroup = 2;
        }
        // in windowed mode, LDJ will always launch two windows, no special handling needed here
    } else if (avs::game::is_model("KFC")) {
        if (GRAPHICS_WINDOWED & GRAPHICS_PREVENT_SECONDARY_WINDOW) {
            // user wants windowed mode but does not want subscreen at all
            pCaps->NumberOfAdaptersInGroup = 1;
        } else {
            // in both full screen and windowed mode, use 2 so that the game draws the subscreen
            // (if this is 1, the game won't even draw the second window, causing subscreen overlay to not work)
            pCaps->NumberOfAdaptersInGroup = 2;
        }
    } else if (avs::game::is_model({"NBT", "PAN"})) {
        // beatstream, nostalgia
        if (GRAPHICS_WINDOWED) {
            pCaps->NumberOfAdaptersInGroup = std::min(pCaps->NumberOfAdaptersInGroup, 1u);
        } else if (GRAPHICS_FORCE_SINGLE_ADAPTER) {
            pCaps->NumberOfAdaptersInGroup = 1;
        }
    }

    return ret;
}

HMONITOR STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterMonitor(UINT Adapter) {
    return pReal->GetAdapterMonitor(Adapter);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CreateDevice(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        HWND hFocusWindow,
        DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        IDirect3DDevice9 **ppReturnedDeviceInterface)
{
    log_misc("graphics::d3d9", "IDirect3D9::CreateDevice hook hit ({}, {}, {}, {}, {}, {})",
            Adapter,
            DeviceType,
            fmt::ptr(hFocusWindow),
            behavior2s(BehaviorFlags),
            fmt::ptr(pPresentationParameters),
            fmt::ptr(ppReturnedDeviceInterface));

    // check parameters
    if (!pPresentationParameters || !ppReturnedDeviceInterface) {
        log_warning("graphics::d3d9", "NULL pointer passed in for required parameter");
        return D3DERR_INVALIDCALL;
    }

    DWORD orig_behavior_flags = BehaviorFlags;
    size_t num_adapters = 1;

    // behavior flags
    if (D3D9_BEHAVIOR_DISABLE) {
        BehaviorFlags &= ~D3D9_BEHAVIOR_DISABLE;
    }

    // when rendering via software, disable pure device
    if (BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) {
        BehaviorFlags &= ~D3DCREATE_PUREDEVICE;
    }

    // override adapter used
    if (D3D9_ADAPTER.has_value()) {
        Adapter = D3D9_ADAPTER.value();
    }

    // get number of adapters for info dump
    if (orig_behavior_flags & D3DCREATE_ADAPTERGROUP_DEVICE) {
        D3DCAPS9 device_caps {};

        if (SUCCEEDED(this->pReal->GetDeviceCaps(Adapter, DeviceType, &device_caps))) {
            num_adapters = device_caps.NumberOfAdaptersInGroup;
        }
    }

    // dump presentation parameters
    for (size_t i = 0; i < num_adapters; i++) {
        auto *params = &pPresentationParameters[i];

        if (!GRAPHICS_WINDOWED && i == 0 && GRAPHICS_FS_CUSTOM_RESOLUTION.has_value()) {
            params->BackBufferWidth = GRAPHICS_FS_CUSTOM_RESOLUTION.value().first;
            params->BackBufferHeight = GRAPHICS_FS_CUSTOM_RESOLUTION.value().second;
        }

        log_info("graphics::d3d9",
                "D3D9 presentation parameters for adapter {}: BackBufferWidth: {}, BackBufferHeight: {}, "
                "Format: {}, BackBufferCount: {}, MultiSampleType: {}, MultiSampleQuality: {}, "
                "SwapEffect: {}, Windowed: {}, EnableAutoDepthStencil: {}, AutoDepthStencilFormat: {}, "
                "Flags: {}, FullScreen_RefreshRateInHz: {}, PresentationInterval: {}",
                i,
                params->BackBufferWidth,
                params->BackBufferHeight,
                format2s(params->BackBufferFormat),
                params->BackBufferCount,
                params->MultiSampleType,
                params->MultiSampleQuality,
                params->SwapEffect,
                params->Windowed,
                params->EnableAutoDepthStencil,
                format2s(params->AutoDepthStencilFormat),
                params->Flags,
                params->FullScreen_RefreshRateInHz,
                presentation_interval2s(params->PresentationInterval));
    }

    // set windowed
    if (GRAPHICS_WINDOWED) {
        pPresentationParameters->Windowed = true;
        pPresentationParameters->FullScreen_RefreshRateInHz = 0;
        update_backbuffer_dimensions(pPresentationParameters);

    } else if (GRAPHICS_FORCE_REFRESH > 0) {
        log_info("graphics::d3d9", "force refresh rate: {} => {} Hz (-graphics-force-refresh option)",
                pPresentationParameters->FullScreen_RefreshRateInHz,
                GRAPHICS_FORCE_REFRESH);

        pPresentationParameters->FullScreen_RefreshRateInHz = GRAPHICS_FORCE_REFRESH;

    } else if (pPresentationParameters->FullScreen_RefreshRateInHz == 0) {
        log_warning(
            "graphics::d3d9",
            "This game sets FullScreen_RefreshRateInHz to 0, which means it will boot with whatever "
            "refresh rate you have set in the desktop. If the game is launching at the wrong Hz, "
            "either use -graphics-force-refresh option or change the desktop resolution beforehand.");
    }

    // force single adapter
    if (GRAPHICS_FORCE_SINGLE_ADAPTER) {
        log_info("graphics::d3d9", "disabling adapter group device with force single adapter mode");

        D3D9_BEHAVIOR_DISABLE |= D3DCREATE_ADAPTERGROUP_DEVICE;
        BehaviorFlags &= ~D3DCREATE_ADAPTERGROUP_DEVICE;
    }

    if (GRAPHICS_FORCE_VSYNC_BUFFER.has_value()) {
        log_info("graphics::d3d9", "force BackBufferCount: {} => {}",
            pPresentationParameters->BackBufferCount,
            GRAPHICS_FORCE_VSYNC_BUFFER.value());
        pPresentationParameters->BackBufferCount = GRAPHICS_FORCE_VSYNC_BUFFER.value();
    }

    // call original
    HRESULT ret = this->pReal->CreateDevice(
            Adapter,
            DeviceType,
            hFocusWindow,
            BehaviorFlags,
            pPresentationParameters,
            ppReturnedDeviceInterface);

    // check for error
    if (ret != D3D_OK) {

        // log error
        log_info("graphics::d3d9", "IDirect3D9::CreateDevice failed, hr={}", FMT_HRESULT(ret));
    } else if (!D3D9_DEVICE_HOOK_DISABLE) {
        graphics_hook_window(hFocusWindow, pPresentationParameters);

        *ppReturnedDeviceInterface = new WrappedIDirect3DDevice9(hFocusWindow, *ppReturnedDeviceInterface);
    }

    // return result
    return ret;
}

/*
 * IDirect3D9Ex
 */

UINT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter) {
    assert(is_d3d9ex);

    return static_cast<IDirect3D9Ex *>(pReal)->GetAdapterModeCountEx(Adapter, pFilter);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::EnumAdapterModesEx(
        UINT Adapter,
        const D3DDISPLAYMODEFILTER *pFilter,
        UINT Mode,
        D3DDISPLAYMODEEX *pMode)
{
    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3D9Ex *>(pReal)->EnumAdapterModesEx(Adapter, pFilter, Mode, pMode));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterDisplayModeEx(
        UINT Adapter,
        D3DDISPLAYMODEEX *pMode,
        D3DDISPLAYROTATION *pRotation)
{
    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3D9Ex *>(pReal)->GetAdapterDisplayModeEx(Adapter, pMode, pRotation));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CreateDeviceEx(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        HWND hFocusWindow,
        DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        D3DDISPLAYMODEEX *pFullscreenDisplayMode,
        IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
    assert(is_d3d9ex);

    log_misc("graphics::d3d9", "IDirect3D9Ex::CreateDeviceEx hook hit ({}, {}, {}, {}, {}, {})",
             Adapter,
             DeviceType,
             fmt::ptr(hFocusWindow),
             behavior2s(BehaviorFlags),
             fmt::ptr(pPresentationParameters),
             fmt::ptr(ppReturnedDeviceInterface));

    // check parameters
    if (!pPresentationParameters || !ppReturnedDeviceInterface) {
        log_warning("graphics::d3d9", "NULL pointer passed in required parameter");

        return D3DERR_INVALIDCALL;
    }

    DWORD orig_behavior_flags = BehaviorFlags;
    size_t num_adapters = 1;

    // behavior flags
    if (D3D9_BEHAVIOR_DISABLE) {
        BehaviorFlags &= ~D3D9_BEHAVIOR_DISABLE;
    }

    // when rendering via software, disable pure device
    if (BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) {
        BehaviorFlags &= ~D3DCREATE_PUREDEVICE;
    }

    // override adapter used
    if (D3D9_ADAPTER.has_value()) {
        Adapter = D3D9_ADAPTER.value();
    }

    // get number of adapters for info dump
    if (orig_behavior_flags & D3DCREATE_ADAPTERGROUP_DEVICE) {
        D3DCAPS9 device_caps {};

        if (SUCCEEDED(this->pReal->GetDeviceCaps(Adapter, DeviceType, &device_caps))) {
            num_adapters = device_caps.NumberOfAdaptersInGroup;
        }
    }

    for (size_t i = 0; i < num_adapters; i++) {
        auto *params = &pPresentationParameters[i];

        if (!GRAPHICS_WINDOWED && i == 0 && GRAPHICS_FS_CUSTOM_RESOLUTION.has_value()) {
            params->BackBufferWidth = GRAPHICS_FS_CUSTOM_RESOLUTION.value().first;
            params->BackBufferHeight = GRAPHICS_FS_CUSTOM_RESOLUTION.value().second;
        }

        log_info("graphics::d3d9",
                "D3D9Ex presentation parameters for adapter {}: BackBufferWidth: {}, BackBufferHeight: {}, "
                "Format: {}, BackBufferCount: {}, MultiSampleType: {}, MultiSampleQuality: {}, "
                "SwapEffect: {}, Windowed: {}, EnableAutoDepthStencil: {}, AutoDepthStencilFormat: {}, "
                "Flags: {}, FullScreen_RefreshRateInHz: {}, PresentationInterval: {}",
                i,
                params->BackBufferWidth,
                params->BackBufferHeight,
                format2s(params->BackBufferFormat),
                params->BackBufferCount,
                params->MultiSampleType,
                params->MultiSampleQuality,
                params->SwapEffect,
                params->Windowed,
                params->EnableAutoDepthStencil,
                format2s(params->AutoDepthStencilFormat),
                params->Flags,
                params->FullScreen_RefreshRateInHz,
                presentation_interval2s(params->PresentationInterval));
    }
    if (pFullscreenDisplayMode) {
        for (size_t i = 0; i < num_adapters; i++) {
            auto *fullscreen_display_mode = &pFullscreenDisplayMode[i];

            if (!GRAPHICS_WINDOWED && i == 0 && GRAPHICS_FS_CUSTOM_RESOLUTION.has_value()) {
                fullscreen_display_mode->Width = GRAPHICS_FS_CUSTOM_RESOLUTION.value().first;
                fullscreen_display_mode->Height = GRAPHICS_FS_CUSTOM_RESOLUTION.value().second;
            }

            log_info("graphics::d3d9",
                    "D3D9Ex fullscreen display mode for adapter {}: Width: {}, Height: {}, RefreshRate: {}, "
                    "Format: {}, ScanLineOrdering: {}",
                    i,
                    fullscreen_display_mode->Width,
                    fullscreen_display_mode->Height,
                    fullscreen_display_mode->RefreshRate,
                    format2s(fullscreen_display_mode->Format),
                    fullscreen_display_mode->ScanLineOrdering);
        }
    }

    // set windowed
    //
    // note from MSDN: `pFullscreenDisplayMode` must be NULL for windowed mode.
    if (GRAPHICS_WINDOWED) {
        if (avs::game::is_model({"LDJ", "KFC"}) && (BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE)) {
            log_misc("graphics::d3d9", "disabling adapter group device in windowed mode");

            D3D9_BEHAVIOR_DISABLE |= D3DCREATE_ADAPTERGROUP_DEVICE;
            BehaviorFlags &= ~D3DCREATE_ADAPTERGROUP_DEVICE;
        }

        pPresentationParameters->Windowed = true;
        pPresentationParameters->FullScreen_RefreshRateInHz = 0;
        update_backbuffer_dimensions(pPresentationParameters);
        pFullscreenDisplayMode = nullptr;
    } else if (GRAPHICS_FORCE_REFRESH > 0) {
        log_info("graphics::d3d9", "force refresh rate: {} => {} Hz",
                pPresentationParameters->FullScreen_RefreshRateInHz,
                GRAPHICS_FORCE_REFRESH);

        pPresentationParameters->FullScreen_RefreshRateInHz = GRAPHICS_FORCE_REFRESH;

        if (pFullscreenDisplayMode) {
            pFullscreenDisplayMode->RefreshRate = GRAPHICS_FORCE_REFRESH;
        }
    }

    // force single adapter
    if (GRAPHICS_FORCE_SINGLE_ADAPTER) {
        log_info("graphics::d3d9", "disabling adapter group device with force single adapter mode");

        D3D9_BEHAVIOR_DISABLE |= D3DCREATE_ADAPTERGROUP_DEVICE;
        BehaviorFlags &= ~D3DCREATE_ADAPTERGROUP_DEVICE;
    }

    if (GRAPHICS_FORCE_VSYNC_BUFFER.has_value()) {
        log_info("graphics::d3d9", "force BackBufferCount: {} => {}",
            pPresentationParameters->BackBufferCount,
            GRAPHICS_FORCE_VSYNC_BUFFER.value());
        pPresentationParameters->BackBufferCount = GRAPHICS_FORCE_VSYNC_BUFFER.value();
    }

    // call original
    HRESULT result = static_cast<IDirect3D9Ex *>(this->pReal)->CreateDeviceEx(
            Adapter,
            DeviceType,
            hFocusWindow,
            BehaviorFlags,
            pPresentationParameters,
            pFullscreenDisplayMode,
            ppReturnedDeviceInterface);

    // check for error
    if (result != D3D_OK) {

        // log error
        log_warning("graphics::d3d9", "CreateDeviceEx failed, hr={}", FMT_HRESULT(result));
    } else if (!D3D9_DEVICE_HOOK_DISABLE) {
        graphics_hook_window(hFocusWindow, pPresentationParameters);

        *ppReturnedDeviceInterface = new WrappedIDirect3DDevice9(hFocusWindow, *ppReturnedDeviceInterface);

        // initialize sub screen if IIDX/SDVX requested a multi-head context
        if (avs::game::is_model({"LDJ", "KFC"}) && (orig_behavior_flags & D3DCREATE_ADAPTERGROUP_DEVICE)) {
            graphics_d3d9_ldj_init_sub_screen(*ppReturnedDeviceInterface, &pPresentationParameters[1]);
        }
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterLUID(UINT Adapter, LUID *pLUID) {
    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3D9Ex *>(pReal)->GetAdapterLUID(Adapter, pLUID));
}

// Create swap chain for TDJ sub screen if needed
//
// The sub screen swap chain should be created if:
// - Running windowed with `NumberOfAdaptersInGroup >= 2` (game expects implicit swap chain to exist)
// - Running fullscreen with `NumberOfAdaptersInGroup < 2` (overridden `GetDeviceCaps` structure)
static void graphics_d3d9_ldj_init_sub_screen(IDirect3DDevice9Ex *device, D3DPRESENT_PARAMETERS *present_params) {
    D3DCAPS9 caps {};
    HRESULT hr = device->GetDeviceCaps(&caps);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to get device caps, hr={}", FMT_HRESULT(hr));
        return;
    }

    // check if sub window swap chain is needed
    /*
    if (GRAPHICS_WINDOWED && caps.NumberOfAdaptersInGroup < 2) {
        log_info("graphics::d3d9", "skipping swap chain creation, running windowed with less than two monitors");
        return;
    }
    if (!GRAPHICS_WINDOWED && caps.NumberOfAdaptersInGroup >= 2) {
        log_info("graphics::d3d9", "skipping swap chain creation, running fullscreen with two or more monitors");
        return;
    }
    */

    if (GRAPHICS_WINDOWED) {
        log_info("graphics::d3d9", "creating additional swap chain");

        present_params->Windowed = true;
        present_params->FullScreen_RefreshRateInHz = 0;

        // calling `WrappedIDirect3DDevice9::CreateAdditionalSwapChain` triggers special handling for
        // LDJ calling `GetSwapChain`
        hr = device->CreateAdditionalSwapChain(present_params, &SUB_SWAP_CHAIN);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9", "failed to create additional swap chain, hr={}", FMT_HRESULT(hr));
        }
    } else {
        hr = device->GetSwapChain(1, &SUB_SWAP_CHAIN);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9", "failed to acquire fullscreen sub swap chain, hr={}", FMT_HRESULT(hr));
        } else {
            return;
        }

        hr = device->CreateAdditionalSwapChain(present_params, &SUB_SWAP_CHAIN);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9", "failed to get additional swap chain, hr={}", FMT_HRESULT(hr));
        }
    }
}

IDirect3DSurface9 *graphics_d3d9_ldj_get_sub_screen() {
    if (SUB_SWAP_CHAIN == nullptr) {
        return nullptr;
    }

    IDirect3DSurface9 *surface = nullptr;
    HRESULT hr = SUB_SWAP_CHAIN->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &surface);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to get back buffer of LDJ sub screen, hr={}", FMT_HRESULT(hr));

        return nullptr;
    }

    return surface;
}

static void graphics_d3d9_ldj_on_present(IDirect3DDevice9 *wrapped_device) {
    if (!ATTEMPTED_SUB_SWAP_CHAIN_ACQUIRE && SUB_SWAP_CHAIN == nullptr) {
        ATTEMPTED_SUB_SWAP_CHAIN_ACQUIRE = true;
        HRESULT hr = wrapped_device->GetSwapChain(1, &SUB_SWAP_CHAIN);
        if (FAILED(hr)) {
            log_warning(
                "graphics::d3d9",
                "failed to acquire sub screeen swap chain! hr={}",
                FMT_HRESULT(hr));
            return;
        }
    }

    if (SUB_SWAP_CHAIN != nullptr) {
        wintouchemu::update();

        if (GRAPHICS_WINDOWED || SUBSCREEN_FORCE_REDRAW) {
            SUB_SWAP_CHAIN->Present(nullptr, nullptr, nullptr, nullptr, 0);
        }
    }
}

static void save_capture(
        int screen,
        D3DFORMAT format,
        UINT width,
        UINT height,
        IDirect3DSurface9 *surface) {
    HRESULT hr;

    // lock surface to be able to access the data
    D3DLOCKED_RECT finished_copy {};
    hr = surface->LockRect(&finished_copy, nullptr, 0);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to lock screenshot surface, hr={}", FMT_HRESULT(hr));
        graphics_capture_skip(screen);
        return;
    }

    // copy pixel data
    size_t pitch = finished_copy.Pitch;
    auto data = reinterpret_cast<uint8_t *>(finished_copy.pBits);
    auto pixels = new uint8_t[width * height * 3];
    for (size_t row = 0; row < height; row++) {
        size_t offset_pixels = 0;
        size_t offset_row = row * width * 3;
        switch (format) {
            case D3DFMT_R8G8B8: {
                for (size_t offset = 0; offset < pitch; offset += 3) {
                    auto cell = data + row * pitch + offset;
                    auto pixel = &pixels[offset_row + offset_pixels];
                    pixel[0] = cell[0];
                    pixel[1] = cell[1];
                    pixel[2] = cell[2];
                    offset_pixels += 3;
                }
                break;
            }
            case D3DFMT_X8R8G8B8:
            case D3DFMT_A8R8G8B8: {
                for (size_t offset = 0; offset < pitch; offset += 4) {
                    auto cell = data + row * pitch + offset;
                    auto pixel = &pixels[offset_row + offset_pixels];
                    pixel[0] = cell[2];
                    pixel[1] = cell[1];
                    pixel[2] = cell[0];
                    offset_pixels += 3;
                }
                break;
            }
            case D3DFMT_X8B8G8R8:
            case D3DFMT_A8B8G8R8: {
                for (size_t offset = 0; offset < pitch; offset += 4) {
                    auto cell = data + row * pitch + offset;
                    auto pixel = &pixels[offset_row + offset_pixels];
                    pixel[0] = cell[0];
                    pixel[1] = cell[1];
                    pixel[2] = cell[2];
                    offset_pixels += 3;
                }
                break;
            }
            default: {
                for (size_t offset = 0; offset < width; offset++) {
                    auto pixel = &pixels[offset_row + offset_pixels];
                    pixel[0] = 0;
                    pixel[1] = 0;
                    pixel[2] = 0;
                    offset_pixels += 3;
                }
            }
        }
    }

    // unlock surface
    hr = surface->UnlockRect();
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to unlock screenshot surface, hr={}", FMT_HRESULT(hr));
        graphics_capture_skip(screen);
        return;
    }

    // enqueue
    graphics_capture_enqueue(screen, pixels, width, height);
}

static void save_screenshot(const std::string &file_path, UINT height, IDirect3DSurface9 *surface) {
    HRESULT hr;

    D3DLOCKED_RECT finished_copy {};
    hr = surface->LockRect(&finished_copy, nullptr, 0);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to lock screenshot surface, hr={}", FMT_HRESULT(hr));
        return;
    }

    // set alpha channel to 255
    {
        auto pitch = finished_copy.Pitch;
        auto data = reinterpret_cast<uint8_t *>(finished_copy.pBits);

        for (size_t i = 0; i < height; i++) {
            for (int j = 3; j < pitch; j += 4) {
                data[i * pitch + j] = 255;
            }
        }
    }

    hr = surface->UnlockRect();
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to unlock screenshot surface, hr={}", FMT_HRESULT(hr));
        return;
    }

    // lazy load function
    static D3DXSaveSurfaceToFileA_t D3DXSaveSurfaceToFileA_ptr = nullptr;
    if (D3DXSaveSurfaceToFileA_ptr == nullptr) {
        D3DXSaveSurfaceToFileA_ptr = libutils::try_proc<D3DXSaveSurfaceToFileA_t>("D3DXSaveSurfaceToFileA");

        // check if function was not found, likely because d3dx9 is not loaded
        if (!ATTEMPTED_D3DX9_LOAD_LIBRARY && D3DXSaveSurfaceToFileA_ptr == nullptr) {
            ATTEMPTED_D3DX9_LOAD_LIBRARY = true;

            for (size_t i = 43; i >= 24; i--) {
                auto lib_name = fmt::format("d3dx9_{}.dll", i);
                auto d3dx9 = libutils::try_library(lib_name);

                // Check if library was not found
                if (d3dx9 == nullptr) {
                    continue;
                }

                D3DXSaveSurfaceToFileA_ptr = libutils::try_proc<D3DXSaveSurfaceToFileA_t>(
                        d3dx9, "D3DXSaveSurfaceToFileA");

                // Check if function was not found
                if (D3DXSaveSurfaceToFileA_ptr == nullptr) {
                    FreeLibrary(d3dx9);
                    d3dx9 = nullptr;

                    continue;
                }

                log_info("graphics::d3d9", "found surface save function in '{}'", lib_name);
                break;
            }
        }
    }

    if (D3DXSaveSurfaceToFileA_ptr != nullptr) {

        // save to file
        log_info("graphics::d3d9", "saving screenshot to {}", file_path);
        auto hr = D3DXSaveSurfaceToFileA_ptr(file_path.c_str(), D3DXIFF_PNG, surface, nullptr, nullptr);

        if (FAILED(hr)) {
            log_warning("graphics::d3d9", "Failed to save screenshot");
            return;
        }

        // save to clipboard
        clipboard::copy_image(file_path);
    } else {
        log_warning("graphics::d3d9", "Direct3D save helper function not available");
    }
}

void graphics_d3d9_on_present(
        HWND hFocusWindow,
        IDirect3DDevice9 *device,
        IDirect3DDevice9 *wrapped_device) {

    // Do overlay init as many d3d9 hooks create a dummy instance to get vtable offsets and never
    // call `Present`. This avoids race conditions on `IDirect3D9::CreateDevice` like with
    // `dx9osd.dll` for pfreepanic.
    if (!overlay::OVERLAY) {
        IDirect3D9 *d3d = nullptr;

        if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d != nullptr) {
            overlay::create_d3d9(hFocusWindow, d3d, device);
            d3d->Release();
        }
    } else if (overlay::OVERLAY->uses_device(device) && SUCCEEDED(device->BeginScene())) {

        // render overlay
        overlay::OVERLAY->update();
        overlay::OVERLAY->new_frame();
        overlay::OVERLAY->render();
        device->EndScene();
    }

    // for IIDX TDJ / SDVX UFC, handle subscreen
    const bool is_vm = 
        avs::game::is_model("KFC") &&
        (avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H');
    const bool is_tdj = avs::game::is_model("LDJ") && games::iidx::TDJ_MODE;
    if (is_vm || is_tdj) {
        graphics_d3d9_ldj_on_present(wrapped_device);
    }

    // check screenshot key
    static bool trigger_last = false;
    auto buttons = games::get_buttons_overlay(eamuse_get_game());
    if (buttons && (!overlay::OVERLAY || overlay::OVERLAY->hotkeys_triggered()) &&
            GameAPI::Buttons::getState(RI_MGR, buttons->at(games::OverlayButtons::Screenshot)))
    {
        if (!trigger_last) {
            graphics_screenshot_trigger();
        }
        trigger_last = true;
    } else {
        trigger_last = false;
    }

    // process pending screenshot
    bool screenshot = false;
    bool capture = false;
    int capture_screen = 0;
    if ((screenshot = graphics_screenshot_consume())
    || ((capture = graphics_capture_consume(&capture_screen)))) {
        HRESULT hr = S_OK;

        // TODO: verify capture_screen is a valid swapchain

        // get back buffer
        IDirect3DSurface9 *buffer = nullptr;
        if (SUB_SWAP_CHAIN != nullptr && capture_screen & 1) {
            hr = SUB_SWAP_CHAIN->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &buffer);
        } else {
            hr = device->GetBackBuffer(capture_screen, 0, D3DBACKBUFFER_TYPE_MONO, &buffer);
        }
        if (FAILED(hr) || buffer == nullptr) {
            log_warning("graphics::d3d9",
                    "failed to get back buffer, hr={}",
                    FMT_HRESULT(hr));
            return;
        }

        D3DSURFACE_DESC desc {};
        hr = buffer->GetDesc(&desc);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9",
                    "failed to acquire back buffer descriptor, hr={}",
                    FMT_HRESULT(hr));
            buffer->Release();
            return;
        }

        // TODO: cache render targets
        IDirect3DSurface9 *temp_surface = nullptr;
        hr = device->CreateRenderTarget(
                desc.Width, desc.Height, desc.Format, desc.MultiSampleType,
                desc.MultiSampleQuality, TRUE, &temp_surface, nullptr);
        if (FAILED(hr) || temp_surface == nullptr) {
            log_warning("graphics::d3d9",
                    "failed to acquire temporary surface, hr={}",
                    FMT_HRESULT(hr));
            buffer->Release();
            return;
        }

        hr = device->StretchRect(buffer, nullptr, temp_surface, nullptr, D3DTEXF_NONE);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9",
                    "failed to copy back buffer contents, hr={}",
                    FMT_HRESULT(hr));
            temp_surface->Release();
            buffer->Release();
            return;
        }

        // release original back buffer reference
        buffer->Release();

        // function for storing the surface
        auto surface_process = [=]() {

            // capture
            if (capture) {
                save_capture(capture_screen, desc.Format, desc.Width, desc.Height, temp_surface);
            }

            // screenshot
            if (screenshot) {

                // check where we can save it
                auto file_path = graphics_screenshot_genpath();
                if (!file_path.empty()) {

                    // write to file
                    save_screenshot(file_path, desc.Height, temp_surface);
                }
            }

            // release surface
            temp_surface->Release();
        };

        // list of games that crash when running the screenshot processor on another thread
        static const robin_hood::unordered_set<std::string> THREAD_BAN {
                "JMA",
#ifndef SPICE64
                "KFC",
#endif
                "KMA",
                "KLP",
                "LMA",
        };

        // run the save operation on another thread for supported games
        if (THREAD_BAN.contains(avs::game::MODEL)) {
            surface_process();
        } else {
            static auto pool = ThreadPool(2);
            pool.add(surface_process);
        }
    }
}

void update_backbuffer_dimensions(D3DPRESENT_PARAMETERS *params) {
    if (!GRAPHICS_WINDOW_BACKBUFFER_SCALE) {
        return;
    }

    // only do this for primary adapter
    static bool first_adapter_hooked = false;
    if (first_adapter_hooked) {
        return;
    }

    if (GRAPHICS_WINDOW_SIZE.has_value()) {
        first_adapter_hooked = true;
        params->BackBufferWidth = GRAPHICS_WINDOW_SIZE.value().first;
        params->BackBufferHeight = GRAPHICS_WINDOW_SIZE.value().second;
        log_info(
            "graphics::d3d9",
            "override BackBufferWidth / BackBufferHeight with {}x{} (from -windowresize)",
            params->BackBufferWidth,
            params->BackBufferHeight);
        return;
    }

    if (cfg::SCREENRESIZE->client_width != 0 && cfg::SCREENRESIZE->client_height != 0) {
        first_adapter_hooked = true;
        params->BackBufferWidth = cfg::SCREENRESIZE->client_width;
        params->BackBufferHeight = cfg::SCREENRESIZE->client_height;
        log_info(
            "graphics::d3d9",
            "override BackBufferWidth / BackBufferHeight with {}x{} (from screen resize config file)",
            params->BackBufferWidth,
            params->BackBufferHeight);
        return;
    }
}