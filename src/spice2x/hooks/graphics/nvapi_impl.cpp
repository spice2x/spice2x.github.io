#include "nvapi_impl.h"

#ifdef SPICE64

#include <algorithm>
#include <vector>

#include <windows.h>

#include "external/nvapi/nvapi.h"
#include "hooks/libraryhook.h"
#include "util/logging.h"
#include "util/sysutils.h"

namespace nvapi_impl {

namespace {

constexpr unsigned int NVAPI_INITIALIZE_ID = 0x0150E828;
constexpr unsigned int NVAPI_INITIALIZE_EX_ID = 0xAD298D3F;
constexpr unsigned int NVAPI_UNLOAD_ID = 0xD22BDD7E;
constexpr unsigned int NVAPI_ENUM_PHYSICAL_GPUS_ID = 0xE5AC921F;
constexpr unsigned int NVAPI_GPU_GET_CONNECTED_DISPLAY_IDS_ID = 0x0078DBA2;
constexpr unsigned int NVAPI_DISP_GET_GDI_PRIMARY_DISPLAY_ID = 0x1E9D8A31;
constexpr unsigned int NVAPI_DISP_GET_DISPLAY_CONFIG_ID = 0x11ABCCF8;
constexpr unsigned int NVAPI_DISP_SET_DISPLAY_CONFIG_ID = 0x5D8CF8DE;

constexpr char NVAPI_DLL_NAME_A[] = "nvapi64.dll";

struct SyntheticDisplay {
    NvU32 display_id;
    NvU32 width;
    NvU32 height;
    NvU32 color_depth;
    NvS32 x;
    NvS32 y;
    NvU32 refresh_rate_1k;
    NV_ROTATE rotation;
    bool primary;
};

static bool provider_initialized = false;
static bool nvapi_initialized = false;
static int gpu_handle_storage = 0;
// snapshot of the Win32 display state exposed through synthetic NVAPI
static std::vector<SyntheticDisplay> displays;

static NvPhysicalGpuHandle get_gpu_handle() {
    return reinterpret_cast<NvPhysicalGpuHandle>(&gpu_handle_storage);
}

static NV_ROTATE get_rotation(DWORD orientation) {
    switch (orientation) {
        case DMDO_90:
            return NV_ROTATE_90;
        case DMDO_180:
            return NV_ROTATE_180;
        case DMDO_270:
            return NV_ROTATE_270;
        default:
            return NV_ROTATE_0;
    }
}

static std::vector<SyntheticDisplay> enumerate_displays(
        uint32_t main_refresh_hz,
        uint32_t sub_refresh_hz) {

    std::vector<SyntheticDisplay> result;

    // reuse the active monitor list, then read live modes after -mainmonitor changes
    for (const auto &monitor : sysutils::enumerate_monitors()) {
        DEVMODEA mode {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExA(
                monitor.display_name.c_str(),
                ENUM_CURRENT_SETTINGS,
                &mode,
                0)) {
            continue;
        }

        const bool primary = mode.dmPosition.x == 0 && mode.dmPosition.y == 0;
        result.push_back({
            .display_id = 0,
            .width = mode.dmPelsWidth,
            .height = mode.dmPelsHeight,
            .color_depth = mode.dmBitsPerPel > 0 ? mode.dmBitsPerPel : 32,
            .x = mode.dmPosition.x,
            .y = mode.dmPosition.y,
            .refresh_rate_1k = 0,
            .rotation = get_rotation(mode.dmDisplayOrientation),
            .primary = primary,
        });
    }

    std::stable_sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.primary && !right.primary;
    });

    if (result.size() > 2) {
        result.resize(2);
    }

    if (result.empty()) {
        result.push_back({
            .display_id = 0,
            .width = 1920,
            .height = 1080,
            .color_depth = 32,
            .x = 0,
            .y = 0,
            .refresh_rate_1k = 0,
            .rotation = NV_ROTATE_0,
            .primary = true,
        });
    }

    for (size_t index = 0; index < result.size(); index++) {
        auto &display = result[index];
        display.primary = index == 0;
        display.display_id = 0x80000000u | static_cast<NvU32>(index + 1);
        const uint32_t refresh_hz = index == 0 ? main_refresh_hz : sub_refresh_hz;
        display.refresh_rate_1k = refresh_hz * 1000;
    }

    return result;
}

// initializes NVAPI for the calling process.
// marks the synthetic provider initialized without contacting a driver.
static NvAPI_Status __cdecl NvAPI_Initialize_impl() {
    log_misc("nvapi_impl", "NvAPI_Initialize");
    nvapi_initialized = true;
    return NVAPI_OK;
}

// initializes NVAPI with additional client flags.
// accepts the flags and marks the synthetic provider initialized.
static NvAPI_Status __cdecl NvAPI_InitializeEx_impl(NvU32 flags) {
    log_misc("nvapi_impl", "NvAPI_InitializeEx(flags={:#x})", flags);
    nvapi_initialized = true;
    return NVAPI_OK;
}

// releases NVAPI state held for the calling process.
// clears the synthetic initialization state while leaving the provider installed.
static NvAPI_Status __cdecl NvAPI_Unload_impl() {
    log_misc("nvapi_impl", "NvAPI_Unload");
    nvapi_initialized = false;
    return NVAPI_OK;
}

// enumerates physical GPU handles managed by the NVIDIA driver.
// returns one stable synthetic GPU containing all exposed displays.
static NvAPI_Status __cdecl NvAPI_EnumPhysicalGPUs_impl(
        NvPhysicalGpuHandle gpu_handles[NVAPI_MAX_PHYSICAL_GPUS],
        NvU32 *gpu_count) {

    log_misc(
        "nvapi_impl",
        "NvAPI_EnumPhysicalGPUs(handles={}, count={})",
        fmt::ptr(gpu_handles),
        fmt::ptr(gpu_count));

    if (!nvapi_initialized) {
        return NVAPI_API_NOT_INITIALIZED;
    }
    if (gpu_handles == nullptr || gpu_count == nullptr) {
        return NVAPI_INVALID_ARGUMENT;
    }

    gpu_handles[0] = get_gpu_handle();
    *gpu_count = 1;
    log_misc(
        "nvapi_impl",
        "NvAPI_EnumPhysicalGPUs - gpu={}, count={}",
        fmt::ptr(gpu_handles[0]),
        *gpu_count);
    return NVAPI_OK;
}

// returns connected display descriptors for a physical GPU.
// exposes the monitor snapshot as DP primary and HDMI secondary displays.
static NvAPI_Status __cdecl NvAPI_GPU_GetConnectedDisplayIds_impl(
        NvPhysicalGpuHandle gpu_handle,
        NV_GPU_DISPLAYIDS *display_ids,
        NvU32 *display_id_count,
        NvU32 flags) {

    const NvU32 input_count = display_id_count != nullptr ? *display_id_count : 0;
    log_misc(
        "nvapi_impl",
        "NvAPI_GPU_GetConnectedDisplayIds(gpu={}, ids={}, count={}, flags={:#x})",
        fmt::ptr(gpu_handle),
        fmt::ptr(display_ids),
        input_count,
        flags);

    if (!nvapi_initialized) {
        return NVAPI_API_NOT_INITIALIZED;
    }
    if (gpu_handle != get_gpu_handle()) {
        return NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE;
    }
    if (display_id_count == nullptr) {
        return NVAPI_INVALID_ARGUMENT;
    }

    const NvU32 required_count = static_cast<NvU32>(displays.size());
    if (display_ids == nullptr) {
        *display_id_count = required_count;
        log_misc(
            "nvapi_impl",
            "NvAPI_GPU_GetConnectedDisplayIds - required_count={}",
            required_count);
        return NVAPI_OK;
    }

    const NvU32 capacity = *display_id_count;
    *display_id_count = required_count;
    if (capacity < required_count) {
        return NVAPI_INSUFFICIENT_BUFFER;
    }

    for (NvU32 index = 0; index < required_count; index++) {
        const auto &source = displays[index];
        auto &destination = display_ids[index];
        destination = {};
        destination.version = NV_GPU_DISPLAYIDS_VER;
        destination.connectorType = source.primary ?
            NV_MONITOR_CONN_TYPE_DP : NV_MONITOR_CONN_TYPE_HDMI;
        destination.displayId = source.display_id;
        destination.isActive = 1;
        destination.isOSVisible = 1;
        destination.isConnected = 1;
        destination.isPhysicallyConnected = 1;
    }

    log_misc(
        "nvapi_impl",
        "NvAPI_GPU_GetConnectedDisplayIds - returned_count={}",
        required_count);
    return NVAPI_OK;
}

// returns the NVAPI display ID associated with the Windows GDI primary.
// returns the first synthetic display, ordered from the live desktop origin.
static NvAPI_Status __cdecl NvAPI_DISP_GetGDIPrimaryDisplayId_impl(NvU32 *display_id) {
    log_misc(
        "nvapi_impl",
        "NvAPI_DISP_GetGDIPrimaryDisplayId(display_id={})",
        fmt::ptr(display_id));

    if (!nvapi_initialized) {
        return NVAPI_API_NOT_INITIALIZED;
    }
    if (display_id == nullptr || displays.empty()) {
        return NVAPI_INVALID_ARGUMENT;
    }

    *display_id = displays.front().display_id;
    log_misc(
        "nvapi_impl",
        "NvAPI_DISP_GetGDIPrimaryDisplayId - display_id={:#x}",
        *display_id);
    return NVAPI_OK;
}

static void fill_source_mode(
        NV_DISPLAYCONFIG_SOURCE_MODE_INFO *destination,
        const SyntheticDisplay &source) {

    if (destination == nullptr) {
        return;
    }

    *destination = {};
    destination->resolution.width = source.width;
    destination->resolution.height = source.height;
    destination->resolution.colorDepth = source.color_depth;
    destination->colorFormat = NV_FORMAT_A8R8G8B8;
    destination->position.x = source.x;
    destination->position.y = source.y;
    destination->spanningOrientation = NV_DISPLAYCONFIG_SPAN_NONE;
    destination->bGDIPrimary = source.primary ? 1 : 0;
}

static NvAPI_Status fill_target(
        NV_DISPLAYCONFIG_PATH_TARGET_INFO *destination,
        const SyntheticDisplay &source,
        NvU32 target_id) {

    if (destination == nullptr) {
        return NVAPI_OK;
    }

    auto *details = destination->details;
    destination->displayId = source.display_id;
    destination->targetId = target_id;

    if (details == nullptr) {
        return NVAPI_OK;
    }
    if (details->version != NV_DISPLAYCONFIG_PATH_ADVANCED_TARGET_INFO_VER) {
        return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
    }

    *details = {};
    details->version = NV_DISPLAYCONFIG_PATH_ADVANCED_TARGET_INFO_VER;
    details->rotation = source.rotation;
    details->scaling = NV_SCALING_DEFAULT;
    details->refreshRate1K = source.refresh_rate_1k;
    details->timingOverride = NV_TIMING_OVERRIDE_CURRENT;
    return NVAPI_OK;
}

// retrieves the current global display topology through NVAPI's three-pass contract.
// fills caller-owned buffers from the synthetic monitor snapshot and configured rates.
static NvAPI_Status __cdecl NvAPI_DISP_GetDisplayConfig_impl(
        NvU32 *path_info_count,
        NV_DISPLAYCONFIG_PATH_INFO *path_info) {

    const NvU32 input_count = path_info_count != nullptr ? *path_info_count : 0;
    log_misc(
        "nvapi_impl",
        "NvAPI_DISP_GetDisplayConfig(count={}, paths={})",
        input_count,
        fmt::ptr(path_info));

    if (!nvapi_initialized) {
        return NVAPI_API_NOT_INITIALIZED;
    }
    if (path_info_count == nullptr) {
        return NVAPI_INVALID_ARGUMENT;
    }

    const NvU32 required_count = static_cast<NvU32>(displays.size());
    if (path_info == nullptr) {
        *path_info_count = required_count;
        log_misc(
            "nvapi_impl",
            "NvAPI_DISP_GetDisplayConfig - required_count={}",
            required_count);
        return NVAPI_OK;
    }

    const NvU32 capacity = *path_info_count;
    *path_info_count = required_count;
    if (capacity < required_count) {
        return NVAPI_INSUFFICIENT_BUFFER;
    }

    for (NvU32 index = 0; index < required_count; index++) {
        auto &path = path_info[index];
        if (path.version != NV_DISPLAYCONFIG_PATH_INFO_VER2) {
            return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
        }
        if (path.targetInfo != nullptr && path.targetInfoCount < 1) {
            return NVAPI_INSUFFICIENT_BUFFER;
        }

        const auto &display = displays[index];
        path.sourceId = index;
        path.targetInfoCount = 1;
        path.IsNonNVIDIAAdapter = 0;
        path.pOSAdapterID = nullptr;
        fill_source_mode(path.sourceModeInfo, display);

        const NvAPI_Status status = fill_target(path.targetInfo, display, index);
        if (status != NVAPI_OK) {
            return status;
        }
    }

    log_misc(
        "nvapi_impl",
        "NvAPI_DISP_GetDisplayConfig - returned_count={}",
        required_count);
    return NVAPI_OK;
}

// applies a supplied global display topology through the NVIDIA driver.
// accepts the cabinet topology without making any changes to Windows.
static NvAPI_Status __cdecl NvAPI_DISP_SetDisplayConfig_impl(
        NvU32 path_info_count,
        NV_DISPLAYCONFIG_PATH_INFO *path_info,
        NvU32 flags) {

    log_misc(
        "nvapi_impl",
        "NvAPI_DISP_SetDisplayConfig(count={}, paths={}, flags={:#x})",
        path_info_count,
        fmt::ptr(path_info),
        flags);

    if (!nvapi_initialized) {
        return NVAPI_API_NOT_INITIALIZED;
    }

    log_misc("nvapi_impl", "NvAPI_DISP_SetDisplayConfig - return synthetic success");
    return NVAPI_OK;
}

template<typename T>
static uintptr_t *query_result(T function) {
    return reinterpret_cast<uintptr_t *>(function);
}

// resolves an NVAPI function ID to its implementation address.
// exposes only the synthetic entry points used by KFC and rejects all others.
static uintptr_t *__cdecl NvAPI_QueryInterface_impl(unsigned int function_id) {
    uintptr_t *result = nullptr;
    switch (function_id) {
        case NVAPI_INITIALIZE_ID:
            result = query_result(NvAPI_Initialize_impl);
            break;
        case NVAPI_INITIALIZE_EX_ID:
            result = query_result(NvAPI_InitializeEx_impl);
            break;
        case NVAPI_UNLOAD_ID:
            result = query_result(NvAPI_Unload_impl);
            break;
        case NVAPI_ENUM_PHYSICAL_GPUS_ID:
            result = query_result(NvAPI_EnumPhysicalGPUs_impl);
            break;
        case NVAPI_GPU_GET_CONNECTED_DISPLAY_IDS_ID:
            result = query_result(NvAPI_GPU_GetConnectedDisplayIds_impl);
            break;
        case NVAPI_DISP_GET_GDI_PRIMARY_DISPLAY_ID:
            result = query_result(NvAPI_DISP_GetGDIPrimaryDisplayId_impl);
            break;
        case NVAPI_DISP_GET_DISPLAY_CONFIG_ID:
            result = query_result(NvAPI_DISP_GetDisplayConfig_impl);
            break;
        case NVAPI_DISP_SET_DISPLAY_CONFIG_ID:
            result = query_result(NvAPI_DISP_SetDisplayConfig_impl);
            break;
        default:
            break;
    }

    log_misc(
        "nvapi_impl",
        "NvAPI_QueryInterface(0x{:x}) - {}",
        function_id,
        result != nullptr ? "implemented" : "unsupported");
    return result;
}

}

bool initialize(HINSTANCE dll, uint32_t main_refresh_hz, uint32_t sub_refresh_hz) {
    if (provider_initialized) {
        return true;
    }
    if (dll == nullptr) {
        log_warning("nvapi_impl", "invalid synthetic module handle");
        return false;
    }

    displays = enumerate_displays(main_refresh_hz, sub_refresh_hz);
    libraryhook_hook_library(NVAPI_DLL_NAME_A, dll);
    libraryhook_hook_proc("nvapi_QueryInterface", NvAPI_QueryInterface_impl);
    libraryhook_enable();

    provider_initialized = true;
    log_info(
        "nvapi_impl",
        "synthetic {} enabled with {} display(s), main={} Hz, sub={} Hz",
        NVAPI_DLL_NAME_A,
        displays.size(),
        main_refresh_hz,
        sub_refresh_hz);
    return true;
}

}

#endif
