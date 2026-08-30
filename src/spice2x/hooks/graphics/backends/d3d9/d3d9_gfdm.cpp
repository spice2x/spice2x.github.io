#include "d3d9_gfdm.h"

#include <algorithm>
#include <cassert>
#include <mutex>

#include "games/gitadora/gitadora.h"
#include "hooks/graphics/graphics.h"
#include "util/logging.h"

#include "d3d9_device.h"

bool gfdm_two_head_exclusive() {
    return games::gitadora::is_arena_model()
            && games::gitadora::ARENA_TWO_HEAD_EXCLUSIVE
            && !GRAPHICS_WINDOWED;
}

static std::pair<UINT, UINT> gfdm_small_head_size() {
    if (!gfdm_two_head_exclusive()) {
        return { GFDM_SMALL_WIDTH, GFDM_SMALL_HEIGHT };
    }
    return games::gitadora::arena_subscreen_host_size();
}

// scaling is only needed when the head is not the panel size the game draws into
static bool gfdm_small_head_scaled() {
    const auto [width, height] = gfdm_small_head_size();
    return width != GFDM_SMALL_WIDTH || height != GFDM_SMALL_HEIGHT;
}

// Hand the resolved MAIN/SMALL heads back to the game's own array. The SMALL entry keeps
// the portrait size the game asked for: that is what it renders, and the next reset finds
// the SMALL head by matching it.
void gfdm_publish_two_head_parameters(
        D3DPRESENT_PARAMETERS *logical_presentation_parameters,
        D3DDISPLAYMODEEX *logical_fullscreen_display_modes,
        const GfdmTwoHeadDeviceState &state)
{
    if (logical_presentation_parameters == nullptr) {
        return;
    }

    const UINT small = state.logical_small_swapchain;
    const bool publish_modes = logical_fullscreen_display_modes != nullptr
            && state.fullscreen_display_modes != nullptr;

    logical_presentation_parameters[0] = state.presentation_parameters[0];
    logical_presentation_parameters[small] = state.presentation_parameters[1];
    if (publish_modes) {
        logical_fullscreen_display_modes[0] = state.fullscreen_display_modes[0];
        logical_fullscreen_display_modes[small] = state.fullscreen_display_modes[1];
    }

    if (!gfdm_small_head_scaled()) {
        return;
    }

    logical_presentation_parameters[small].BackBufferWidth = GFDM_SMALL_WIDTH;
    logical_presentation_parameters[small].BackBufferHeight = GFDM_SMALL_HEIGHT;
    if (publish_modes) {
        logical_fullscreen_display_modes[small].Width = GFDM_SMALL_WIDTH;
        logical_fullscreen_display_modes[small].Height = GFDM_SMALL_HEIGHT;
    }
}

HRESULT graphics_d3d9_gfdm_select_two_head_group_parameters(
        const D3DPRESENT_PARAMETERS *logical_presentation_parameters,
        const D3DDISPLAYMODEEX *logical_fullscreen_display_modes,
        D3DPRESENT_PARAMETERS *native_presentation_parameters,
        D3DDISPLAYMODEEX *native_fullscreen_display_modes,
        UINT *logical_small_swapchain,
        const char *operation)
{
    if (!gfdm_two_head_exclusive()
            || logical_presentation_parameters == nullptr
            || native_presentation_parameters == nullptr
            || logical_small_swapchain == nullptr)
    {
        return D3DERR_INVALIDCALL;
    }

    UINT small_slot = GFDM_LOGICAL_HEAD_COUNT;
    for (UINT i = 1; i < GFDM_LOGICAL_HEAD_COUNT; i++) {
        const auto &params = logical_presentation_parameters[i];
        if (params.BackBufferWidth != GFDM_SMALL_WIDTH
                || params.BackBufferHeight != GFDM_SMALL_HEIGHT)
        {
            continue;
        }
        if (small_slot != GFDM_LOGICAL_HEAD_COUNT) {
            log_warning(
                    "graphics::d3d9",
                    "two-head exclusive: {} found multiple SMALL slots: {} and {}",
                    operation,
                    small_slot,
                    i);
            return D3DERR_INVALIDCALL;
        }
        small_slot = i;
    }

    if (small_slot == GFDM_LOGICAL_HEAD_COUNT) {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive: {} could not find an 800x1280 logical SMALL descriptor",
                operation);
        return D3DERR_INVALIDCALL;
    }

    if (logical_fullscreen_display_modes != nullptr
            && (logical_fullscreen_display_modes[small_slot].Width != GFDM_SMALL_WIDTH
                    || logical_fullscreen_display_modes[small_slot].Height != GFDM_SMALL_HEIGHT))
    {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive: {} SMALL slot {} mode is {}x{}, expected {}x{}",
                operation,
                small_slot,
                logical_fullscreen_display_modes[small_slot].Width,
                logical_fullscreen_display_modes[small_slot].Height,
                GFDM_SMALL_WIDTH,
                GFDM_SMALL_HEIGHT);
        return D3DERR_INVALIDCALL;
    }

    native_presentation_parameters[0] = logical_presentation_parameters[0];
    native_presentation_parameters[1] = logical_presentation_parameters[small_slot];
    if (logical_fullscreen_display_modes != nullptr
            && native_fullscreen_display_modes != nullptr)
    {
        native_fullscreen_display_modes[0] = logical_fullscreen_display_modes[0];
        native_fullscreen_display_modes[1] = logical_fullscreen_display_modes[small_slot];
    }

    log_info(
            "graphics::d3d9",
            "two-head exclusive: {} selected logical SMALL slot {} -> native group head 1{}",
            operation,
            small_slot,
            logical_presentation_parameters[small_slot].hDeviceWindow == GFDM_SUBSCREEN_WINDOW
                    ? " (named SMALL host)"
                    : "");
    *logical_small_swapchain = small_slot;
    return D3D_OK;
}

bool is_fake_subscreen_adapter(UINT adapter) {
    // MAIN and SMALL remain native D3D heads; LEFT/RIGHT are virtual.
    if (gfdm_two_head_exclusive()) {
        return adapter >= 2 && adapter < GFDM_LOGICAL_HEAD_COUNT;
    }

    return FAKE_SUBSCREEN_ADAPTER && adapter > 0;
}

void get_fake_subscreen_display_mode(
        UINT adapter,
        D3DDISPLAYMODE *mode)
{
    (void) adapter;
    if (gfdm_two_head_exclusive()) {
        mode->Width = GFDM_SIDE_WIDTH;
        mode->Height = GFDM_SIDE_HEIGHT;
    } else {
        mode->Width = 1280;
        mode->Height = 800;
    }
    mode->RefreshRate = 60;
    mode->Format = D3DFMT_X8R8G8B8;
}

void get_fake_subscreen_display_mode_ex(
        UINT adapter,
        D3DDISPLAYMODEEX *mode)
{
    D3DDISPLAYMODE legacy_mode {};
    get_fake_subscreen_display_mode(adapter, &legacy_mode);

    mode->Size = sizeof(*mode);
    mode->Width = legacy_mode.Width;
    mode->Height = legacy_mode.Height;
    mode->RefreshRate = legacy_mode.RefreshRate;
    mode->Format = legacy_mode.Format;
    mode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
}

HRESULT graphics_d3d9_gfdm_remap_two_head_group_parameters(
        D3DPRESENT_PARAMETERS *presentation_parameters,
        D3DDISPLAYMODEEX *fullscreen_display_modes,
        const char *operation)
{
    if (!gfdm_two_head_exclusive()) {
        return D3D_OK;
    }
    if (presentation_parameters == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    auto &secondary = presentation_parameters[1];
    if (secondary.BackBufferWidth != GFDM_SMALL_WIDTH
            || secondary.BackBufferHeight != GFDM_SMALL_HEIGHT)
    {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive: {} expected physical SMALL {}x{} at group slot 1, got {}x{}",
                operation,
                GFDM_SMALL_WIDTH,
                GFDM_SMALL_HEIGHT,
                secondary.BackBufferWidth,
                secondary.BackBufferHeight);
        return D3DERR_INVALIDCALL;
    }

    if (GFDM_SUBSCREEN_WINDOW == nullptr || !IsWindow(GFDM_SUBSCREEN_WINDOW)) {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive: {} cannot use the named SMALL window as physical group head 1",
                operation);
        return D3DERR_INVALIDCALL;
    }
    secondary.hDeviceWindow = GFDM_SUBSCREEN_WINDOW;

    if (fullscreen_display_modes != nullptr
            && (fullscreen_display_modes[1].Width != GFDM_SMALL_WIDTH
                    || fullscreen_display_modes[1].Height != GFDM_SMALL_HEIGHT))
    {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive: {} SMALL head mode is {}x{}, expected 800x1280",
                operation,
                fullscreen_display_modes[1].Width,
                fullscreen_display_modes[1].Height);
        return D3DERR_INVALIDCALL;
    }

    // the game keeps rendering the portrait subscreen; only the head it is scanned out
    // on changes size, and the scaling happens when the head is presented
    if (gfdm_small_head_scaled()) {
        const auto [host_width, host_height] = gfdm_small_head_size();
        log_info(
                "graphics::d3d9",
                "two-head exclusive: {} SMALL head {}x{} -> {}x{}",
                operation,
                secondary.BackBufferWidth,
                secondary.BackBufferHeight,
                host_width,
                host_height);
        secondary.BackBufferWidth = host_width;
        secondary.BackBufferHeight = host_height;
        if (fullscreen_display_modes != nullptr) {
            fullscreen_display_modes[1].Width = host_width;
            fullscreen_display_modes[1].Height = host_height;
        }
    }

    return D3D_OK;
}

void graphics_d3d9_gfdm_align_two_head_refresh_to_desktop(
        IDirect3D9Ex *d3d,
        UINT master_adapter,
        D3DPRESENT_PARAMETERS *presentation_parameters,
        D3DDISPLAYMODEEX *fullscreen_display_modes,
        const char *operation)
{
    if (d3d == nullptr
            || presentation_parameters == nullptr
            || fullscreen_display_modes == nullptr)
    {
        return;
    }

    for (UINT head = 0; head < 2; head++) {
        const bool explicitly_forced =
                (head == 0 && GRAPHICS_FORCE_REFRESH > 0)
                || (head == 1 && GRAPHICS_FORCE_REFRESH_SUB.has_value());
        if (explicitly_forced) {
            continue;
        }

        D3DDISPLAYMODEEX desktop_mode {};
        desktop_mode.Size = sizeof(desktop_mode);
        D3DDISPLAYROTATION rotation = D3DDISPLAYROTATION_IDENTITY;
        const HRESULT result = d3d->GetAdapterDisplayModeEx(
                master_adapter + head,
                &desktop_mode,
                &rotation);
        if (FAILED(result)) {
            log_warning(
                    "graphics::d3d9",
                    "two-head exclusive: {} could not query native head {} desktop refresh, hr={}",
                    operation,
                    head,
                    FMT_HRESULT(result));
            continue;
        }

        const auto &parameters = presentation_parameters[head];
        if (desktop_mode.Width != parameters.BackBufferWidth
                || desktop_mode.Height != parameters.BackBufferHeight
                || desktop_mode.Format != parameters.BackBufferFormat
                || desktop_mode.RefreshRate == 0)
        {
            log_warning(
                    "graphics::d3d9",
                    "two-head exclusive: {} head {} mode differs from the desktop",
                    operation,
                    head);
            continue;
        }

        if (parameters.FullScreen_RefreshRateInHz != desktop_mode.RefreshRate
                || fullscreen_display_modes[head].RefreshRate != desktop_mode.RefreshRate)
        {
            log_info(
                    "graphics::d3d9",
                    "two-head exclusive: {} head {} refresh {} / {} -> {} (rotation {})",
                    operation,
                    head,
                    parameters.FullScreen_RefreshRateInHz,
                    fullscreen_display_modes[head].RefreshRate,
                    desktop_mode.RefreshRate,
                    static_cast<UINT>(rotation));
            presentation_parameters[head].FullScreen_RefreshRateInHz =
                    desktop_mode.RefreshRate;
            fullscreen_display_modes[head].RefreshRate =
                    desktop_mode.RefreshRate;
        }
    }
}

HRESULT validate_gfdm_two_head_exclusive(
        IDirect3D9 *d3d,
        UINT adapter,
        D3DDEVTYPE device_type,
        DWORD behavior_flags,
        const D3DPRESENT_PARAMETERS *presentation_parameters)
{
    if (!(behavior_flags & D3DCREATE_ADAPTERGROUP_DEVICE)) {
        log_warning(
                "graphics::d3d9",
                "two-head mode requires D3DCREATE_ADAPTERGROUP_DEVICE");
        return D3DERR_NOTAVAILABLE;
    }
    if (adapter != 0) {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive mode currently requires master adapter 0, game requested {}",
                adapter);
        return D3DERR_NOTAVAILABLE;
    }
    if (presentation_parameters == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    const UINT native_adapter_count = d3d->GetAdapterCount();
    if (native_adapter_count != 2) {
        log_warning(
                "graphics::d3d9",
                "two-head mode requires two native D3D9 adapters; found {}",
                native_adapter_count);
        return D3DERR_NOTAVAILABLE;
    }

    D3DCAPS9 caps {};
    const HRESULT caps_result = d3d->GetDeviceCaps(adapter, device_type, &caps);
    if (FAILED(caps_result)) {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive mode could not query native adapter caps, hr={}",
                FMT_HRESULT(caps_result));
        return caps_result;
    }
    if (caps.MasterAdapterOrdinal != adapter
            || caps.AdapterOrdinalInGroup != 0
            || caps.NumberOfAdaptersInGroup != 2)
    {
        log_warning(
                "graphics::d3d9",
                "invalid adapter group: master={}, index={}, size={}",
                caps.MasterAdapterOrdinal,
                caps.AdapterOrdinalInGroup,
                caps.NumberOfAdaptersInGroup);
        return D3DERR_NOTAVAILABLE;
    }

    const auto &main = presentation_parameters[0];
    const auto &small_params = presentation_parameters[1];
    const auto [expected_small_width, expected_small_height] = gfdm_small_head_size();
    if (main.Windowed || small_params.Windowed) {
        log_warning(
                "graphics::d3d9",
                "two-head exclusive mode requires both group heads to be fullscreen");
        return D3DERR_INVALIDCALL;
    }
    if (small_params.BackBufferWidth != expected_small_width
            || small_params.BackBufferHeight != expected_small_height)
    {
        log_warning(
                "graphics::d3d9",
                "SMALL head must be {}x{}; got {}x{}",
                expected_small_width,
                expected_small_height,
                small_params.BackBufferWidth,
                small_params.BackBufferHeight);
        return D3DERR_INVALIDCALL;
    }
    if (main.EnableAutoDepthStencil != small_params.EnableAutoDepthStencil) {
        log_warning(
                "graphics::d3d9",
                "two-head mode requires matching automatic depth-stencil settings");
        return D3DERR_INVALIDCALL;
    }
    if (main.EnableAutoDepthStencil
            && (main.AutoDepthStencilFormat != small_params.AutoDepthStencilFormat
                    || main.BackBufferWidth != small_params.BackBufferWidth
                    || main.BackBufferHeight != small_params.BackBufferHeight
                    || main.BackBufferFormat != small_params.BackBufferFormat))
    {
        log_warning(
                "graphics::d3d9",
                "two-head mode cannot use unequal sizes with automatic depth-stencil");
        return D3DERR_INVALIDCALL;
    }

    return D3D_OK;
}

static bool gfdm_find_alternate_small_mode(
        IDirect3D9Ex *d3d,
        UINT adapter,
        const D3DDISPLAYMODEEX &desired,
        D3DDISPLAYMODEEX *alternate)
{
    if (d3d == nullptr || alternate == nullptr) {
        return false;
    }

    D3DDISPLAYMODEFILTER filter {};
    filter.Size = sizeof(filter);
    filter.Format = desired.Format;
    filter.ScanLineOrdering = D3DSCANLINEORDERING_UNKNOWN;
    const UINT count = d3d->GetAdapterModeCountEx(adapter, &filter);
    const bool portrait = desired.Width < desired.Height;

    for (UINT index = 0; index < count; index++) {
        D3DDISPLAYMODEEX mode {};
        mode.Size = sizeof(mode);
        if (FAILED(d3d->EnumAdapterModesEx(adapter, &filter, index, &mode))
                || mode.Width != desired.Width
                || mode.Height != desired.Height
                || mode.Format != desired.Format
                || (mode.RefreshRate == desired.RefreshRate
                        && mode.ScanLineOrdering == desired.ScanLineOrdering))
        {
            continue;
        }
        *alternate = mode;
        return true;
    }

    for (int preserve_orientation = 1; preserve_orientation >= 0; preserve_orientation--) {
        for (int preserve_refresh = 1; preserve_refresh >= 0; preserve_refresh--) {
            for (UINT index = 0; index < count; index++) {
                D3DDISPLAYMODEEX mode {};
                mode.Size = sizeof(mode);
                if (FAILED(d3d->EnumAdapterModesEx(adapter, &filter, index, &mode))
                        || (mode.Width == desired.Width && mode.Height == desired.Height)
                        || mode.Format != desired.Format)
                {
                    continue;
                }
                if (preserve_orientation && (mode.Width < mode.Height) != portrait) {
                    continue;
                }
                if (preserve_refresh && mode.RefreshRate != desired.RefreshRate) {
                    continue;
                }
                *alternate = mode;
                return true;
            }
        }
    }
    return false;
}

static bool gfdm_wait_for_small_mode(
        HWND window,
        const D3DDISPLAYMODEEX &expected)
{
    const HMONITOR monitor =
            window != nullptr ? MonitorFromWindow(window, MONITOR_DEFAULTTONULL) : nullptr;
    MONITORINFOEXA monitor_info {};
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor == nullptr || !GetMonitorInfoA(monitor, &monitor_info)) {
        return false;
    }

    for (UINT poll = 0; poll < 50; poll++) {
        DEVMODEA current {};
        current.dmSize = sizeof(current);
        if (EnumDisplaySettingsExA(
                    monitor_info.szDevice,
                    ENUM_CURRENT_SETTINGS,
                    &current,
                    0)
                && current.dmPelsWidth == expected.Width
                && current.dmPelsHeight == expected.Height
                && (expected.RefreshRate == 0
                        || current.dmDisplayFrequency == expected.RefreshRate))
        {
            return true;
        }
        Sleep(10);
    }
    return false;
}

HRESULT graphics_d3d9_gfdm_recover_two_head_present_mode(
        IDirect3D9Ex *d3d,
        IDirect3DDevice9Ex *device,
        UINT master_adapter,
        const D3DPRESENT_PARAMETERS *desired_parameters,
        const D3DDISPLAYMODEEX *desired_modes)
{
    if (d3d == nullptr
            || device == nullptr
            || desired_parameters == nullptr
            || desired_modes == nullptr)
    {
        return D3DERR_INVALIDCALL;
    }

    D3DDISPLAYMODEEX alternate_small {};
    if (!gfdm_find_alternate_small_mode(
                d3d,
                master_adapter + 1,
                desired_modes[1],
                &alternate_small))
    {
        log_warning("graphics::d3d9", "no alternate SMALL mode is available for recovery");
        return D3DERR_NOTAVAILABLE;
    }

    D3DPRESENT_PARAMETERS temporary_parameters[2] {
            desired_parameters[0],
            desired_parameters[1]};
    D3DDISPLAYMODEEX temporary_modes[2] {
            desired_modes[0],
            desired_modes[1]};
    temporary_parameters[1].BackBufferWidth = alternate_small.Width;
    temporary_parameters[1].BackBufferHeight = alternate_small.Height;
    temporary_parameters[1].BackBufferFormat = alternate_small.Format;
    temporary_parameters[1].FullScreen_RefreshRateInHz = alternate_small.RefreshRate;
    temporary_modes[1] = alternate_small;

    HRESULT temporary_result = device->ResetEx(temporary_parameters, temporary_modes);
    const bool temporary_settled =
            temporary_result == D3D_OK
            && gfdm_wait_for_small_mode(desired_parameters[1].hDeviceWindow, alternate_small);

    D3DPRESENT_PARAMETERS restore_parameters[2] {
            desired_parameters[0],
            desired_parameters[1]};
    D3DDISPLAYMODEEX restore_modes[2] {
            desired_modes[0],
            desired_modes[1]};
    HRESULT restore_result = device->ResetEx(restore_parameters, restore_modes);
    const bool restore_settled =
            restore_result == D3D_OK
            && gfdm_wait_for_small_mode(desired_parameters[1].hDeviceWindow, desired_modes[1]);

    if (temporary_result != D3D_OK) {
        return temporary_result;
    }
    if (restore_result != D3D_OK) {
        return restore_result;
    }
    if (!temporary_settled || !restore_settled) {
        return D3DERR_NOTAVAILABLE;
    }
    return device->CheckDeviceState(desired_parameters[0].hDeviceWindow) == D3D_OK
            ? D3D_OK
            : D3DERR_NOTAVAILABLE;
}

bool WrappedIDirect3DDevice9::is_gfdm_two_head_exclusive() const {
    return ::gfdm_two_head_exclusive();
}

bool WrappedIDirect3DDevice9::is_gfdm_logical_small_swapchain(
        UINT swapchain) const
{
    return swapchain == gfdm_logical_small_swapchain;
}

bool WrappedIDirect3DDevice9::is_gfdm_logical_side_swapchain(
        UINT swapchain) const
{
    return swapchain > 0
            && swapchain < GFDM_LOGICAL_HEAD_COUNT
            && swapchain != gfdm_logical_small_swapchain;
}

size_t WrappedIDirect3DDevice9::gfdm_hidden_side_swapchain_slot(
        UINT swapchain) const
{
    assert(is_gfdm_logical_side_swapchain(swapchain));
    return swapchain < gfdm_logical_small_swapchain
            ? swapchain
            : swapchain - 1;
}

void WrappedIDirect3DDevice9::set_gfdm_logical_group_parameters(
        const D3DPRESENT_PARAMETERS *presentation_parameters)
{
    if (presentation_parameters == nullptr) {
        gfdm_logical_group_parameters_valid = false;
        gfdm_small_head.resolve(nullptr);
        return;
    }
    std::copy_n(
            presentation_parameters,
            gfdm_logical_group_parameters.size(),
            gfdm_logical_group_parameters.begin());
    gfdm_logical_group_parameters_valid = true;
    gfdm_small_head.resolve(
            &gfdm_logical_group_parameters[gfdm_logical_small_swapchain]);
}

FakeIDirect3DSwapChain9 *
WrappedIDirect3DDevice9::ensure_gfdm_hidden_side_swapchain(UINT iSwapChain) {
    assert(is_gfdm_logical_side_swapchain(iSwapChain));

    const size_t index = gfdm_hidden_side_swapchain_slot(iSwapChain);
    if (fake_sub_swapchain[index] == nullptr) {
        D3DPRESENT_PARAMETERS params {};
        if (gfdm_logical_group_parameters_valid) {
            params = gfdm_logical_group_parameters[iSwapChain];
        } else {
            params.BackBufferWidth = 1080;
            params.BackBufferHeight = 1920;
            params.BackBufferFormat = D3DFMT_X8R8G8B8;
            params.BackBufferCount = 1;
            params.MultiSampleType = D3DMULTISAMPLE_NONE;
            params.SwapEffect = D3DSWAPEFFECT_DISCARD;
            params.Windowed = FALSE;
            params.FullScreen_RefreshRateInHz = 60;
            params.EnableAutoDepthStencil = FALSE;
            params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        }
        fake_sub_swapchain[index] = new FakeIDirect3DSwapChain9(
                this,
                &params,
                is_d3d9ex,
                true);
    }
    return fake_sub_swapchain[index];
}

void WrappedIDirect3DDevice9::release_gfdm_hidden_side_swapchains() {
    for (UINT i = 1; i < GFDM_LOGICAL_HEAD_COUNT; i++) {
        if (!is_gfdm_logical_side_swapchain(i)) {
            continue;
        }
        const size_t index = gfdm_hidden_side_swapchain_slot(i);
        if (fake_sub_swapchain[index] != nullptr) {
            fake_sub_swapchain[index]->Release();
            fake_sub_swapchain[index] = nullptr;
        }
    }
}

void GfdmSmallHead::resolve(const D3DPRESENT_PARAMETERS *logical_small_parameters) {
    release();

    active = gfdm_small_head_scaled();
    format = D3DFMT_X8R8G8B8;
    multisample = D3DMULTISAMPLE_NONE;
    multisample_quality = 0;
    if (!active || logical_small_parameters == nullptr) {
        return;
    }

    if (logical_small_parameters->BackBufferFormat != D3DFMT_UNKNOWN) {
        format = logical_small_parameters->BackBufferFormat;
    }
    multisample = logical_small_parameters->MultiSampleType;
    multisample_quality = logical_small_parameters->MultiSampleQuality;
}

void GfdmSmallHead::apply_logical_size(UINT *width, UINT *height) const {
    if (!active) {
        return;
    }
    if (width != nullptr) {
        *width = GFDM_SMALL_WIDTH;
    }
    if (height != nullptr) {
        *height = GFDM_SMALL_HEIGHT;
    }
}

HRESULT GfdmSmallHead::backbuffer(IDirect3DDevice9 *device, IDirect3DSurface9 **out) {
    if (device == nullptr || out == nullptr || !active) {
        return D3DERR_INVALIDCALL;
    }

    if (surface == nullptr) {
        const HRESULT result = device->CreateRenderTarget(
                GFDM_SMALL_WIDTH,
                GFDM_SMALL_HEIGHT,
                format,
                multisample,
                multisample_quality,
                FALSE,
                &surface,
                nullptr);
        if (FAILED(result)) {
            log_warning(
                    "graphics::d3d9",
                    "two-head exclusive: could not create the portrait SMALL surface, hr={}",
                    FMT_HRESULT(result));
            surface = nullptr;
            return result;
        }
        device->ColorFill(surface, nullptr, D3DCOLOR_XRGB(0, 0, 0));
    }

    surface->AddRef();
    *out = surface;
    return D3D_OK;
}

void GfdmSmallHead::release() {
    if (surface != nullptr) {
        surface->Release();
        surface = nullptr;
    }
}

HRESULT GfdmSmallHead::compose(IDirect3DDevice9 *device) {
    IDirect3DSurface9 *proxy = nullptr;
    HRESULT result = backbuffer(device, &proxy);
    if (FAILED(result)) {
        return result;
    }

    IDirect3DSurface9 *head = nullptr;
    result = device->GetBackBuffer(
            GFDM_NATIVE_SMALL_SWAPCHAIN,
            0,
            D3DBACKBUFFER_TYPE_MONO,
            &head);
    if (FAILED(result)) {
        proxy->Release();
        return result;
    }

    D3DSURFACE_DESC desc {};
    result = head->GetDesc(&desc);
    if (SUCCEEDED(result)) {
        const RECT content = games::gitadora::arena_subscreen_content_rect(
                static_cast<LONG>(desc.Width),
                static_cast<LONG>(desc.Height));

        // discard swap effect leaves the whole head undefined every frame, so the bars
        // have to be repainted along with the image
        device->ColorFill(head, nullptr, D3DCOLOR_XRGB(0, 0, 0));
        result = device->StretchRect(proxy, nullptr, head, &content, D3DTEXF_LINEAR);
    }

    head->Release();
    proxy->Release();
    return result;
}

void WrappedIDirect3DDevice9::gfdm_disarm_present_mode_recovery() {
    std::lock_guard<std::mutex> lock(gfdm_recovery_mutex);
    gfdm_recovery_armed = false;
    gfdm_recovery_consumed.store(true, std::memory_order_release);
}

void WrappedIDirect3DDevice9::gfdm_arm_present_mode_recovery(
        const D3DPRESENT_PARAMETERS *native_presentation_parameters,
        const D3DDISPLAYMODEEX *native_fullscreen_display_modes)
{
    if (native_presentation_parameters == nullptr
            || native_fullscreen_display_modes == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(gfdm_recovery_mutex);
    gfdm_recovery_parameters[0] = native_presentation_parameters[0];
    gfdm_recovery_parameters[1] = native_presentation_parameters[1];
    gfdm_recovery_modes[0] = native_fullscreen_display_modes[0];
    gfdm_recovery_modes[1] = native_fullscreen_display_modes[1];
    if (gfdm_recovery_modes[0].Size == 0) {
        gfdm_recovery_modes[0].Size = sizeof(D3DDISPLAYMODEEX);
    }
    if (gfdm_recovery_modes[1].Size == 0) {
        gfdm_recovery_modes[1].Size = sizeof(D3DDISPLAYMODEEX);
    }
    gfdm_recovery_armed =
            !gfdm_recovery_parameters[0].Windowed
            && !gfdm_recovery_parameters[1].Windowed
            && IsWindow(gfdm_recovery_parameters[1].hDeviceWindow);
    gfdm_recovery_consumed.store(
            !gfdm_recovery_armed,
            std::memory_order_release);
}

WrappedIDirect3DDevice9::GfdmPresentModeRecoveryResult
WrappedIDirect3DDevice9::gfdm_recover_present_mode_change(
        HRESULT *failure_result)
{
    if (failure_result != nullptr) {
        *failure_result = D3D_OK;
    }
    if (!is_gfdm_two_head_exclusive() || !is_d3d9ex
            || gfdm_recovery_consumed.exchange(
                    true,
                    std::memory_order_acq_rel))
    {
        return GfdmPresentModeRecoveryResult::NotAttempted;
    }
    if (GetCurrentThreadId() != gfdm_device_creation_thread_id) {
        if (failure_result != nullptr) {
            *failure_result = D3DERR_INVALIDCALL;
        }
        return GfdmPresentModeRecoveryResult::Failed;
    }

    std::array<D3DPRESENT_PARAMETERS, 2> parameters {};
    std::array<D3DDISPLAYMODEEX, 2> modes {};
    {
        std::lock_guard<std::mutex> lock(gfdm_recovery_mutex);
        if (!gfdm_recovery_armed) {
            return GfdmPresentModeRecoveryResult::NotAttempted;
        }
        parameters = gfdm_recovery_parameters;
        modes = gfdm_recovery_modes;
    }

    IDirect3D9 *raw_d3d = nullptr;
    HRESULT result = pReal->GetDirect3D(&raw_d3d);
    IDirect3D9Ex *raw_d3d_ex = nullptr;
    if (SUCCEEDED(result)) {
        result = raw_d3d != nullptr
                ? raw_d3d->QueryInterface(IID_PPV_ARGS(&raw_d3d_ex))
                : E_FAIL;
    }
    D3DDEVICE_CREATION_PARAMETERS creation {};
    if (SUCCEEDED(result)) {
        result = pReal->GetCreationParameters(&creation);
    }
    if (FAILED(result) || raw_d3d_ex == nullptr) {
        if (raw_d3d_ex != nullptr) {
            raw_d3d_ex->Release();
        }
        if (raw_d3d != nullptr) {
            raw_d3d->Release();
        }
        if (failure_result != nullptr) {
            *failure_result = result;
        }
        return GfdmPresentModeRecoveryResult::Failed;
    }

    result = graphics_d3d9_gfdm_recover_two_head_present_mode(
            raw_d3d_ex,
            static_cast<IDirect3DDevice9Ex *>(pReal),
            creation.AdapterOrdinal,
            parameters.data(),
            modes.data());
    raw_d3d_ex->Release();
    raw_d3d->Release();

    if (result == D3DERR_NOTAVAILABLE) {
        return GfdmPresentModeRecoveryResult::NotAttempted;
    }
    if (FAILED(result)) {
        if (failure_result != nullptr) {
            *failure_result = result;
        }
        return GfdmPresentModeRecoveryResult::Failed;
    }
    return GfdmPresentModeRecoveryResult::ReadyToRetry;
}

