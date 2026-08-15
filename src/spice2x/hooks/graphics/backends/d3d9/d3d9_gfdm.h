#pragma once

#include <array>

#include <d3d9.h>

#include "games/gitadora/gitadora.h"

inline constexpr UINT GFDM_SIDE_WIDTH = 1080;
inline constexpr UINT GFDM_SIDE_HEIGHT = 1920;
inline constexpr UINT GFDM_SMALL_WIDTH = games::gitadora::ARENA_SUBSCREEN_WIDTH;
inline constexpr UINT GFDM_SMALL_HEIGHT = games::gitadora::ARENA_SUBSCREEN_HEIGHT;
inline constexpr UINT GFDM_LOGICAL_HEAD_COUNT = 4;
inline constexpr UINT GFDM_NATIVE_SMALL_SWAPCHAIN = 1;

struct GfdmTwoHeadDeviceState {
    explicit GfdmTwoHeadDeviceState(
            D3DPRESENT_PARAMETERS *presentation_parameters,
            D3DDISPLAYMODEEX *fullscreen_display_modes,
            UINT logical_small_swapchain = 2)
        : presentation_parameters(presentation_parameters),
          fullscreen_display_modes(fullscreen_display_modes),
          logical_small_swapchain(logical_small_swapchain) {}

    void use_native_parameters() {
        presentation_parameters = native_presentation_parameters.data();
        fullscreen_display_modes = source_fullscreen_display_modes != nullptr
                ? native_fullscreen_display_modes.data()
                : nullptr;
    }

    std::array<D3DPRESENT_PARAMETERS, 2> native_presentation_parameters {};
    std::array<D3DDISPLAYMODEEX, 2> native_fullscreen_display_modes {};
    std::array<D3DPRESENT_PARAMETERS, 2> recovery_parameters {};
    std::array<D3DDISPLAYMODEEX, 2> recovery_modes {};
    D3DPRESENT_PARAMETERS *presentation_parameters;
    D3DDISPLAYMODEEX *fullscreen_display_modes;
    D3DDISPLAYMODEEX *const source_fullscreen_display_modes = fullscreen_display_modes;
    UINT logical_small_swapchain;
    bool recovery_candidate = false;
};

bool gfdm_two_head_exclusive();
bool is_fake_subscreen_adapter(UINT adapter);
void get_fake_subscreen_display_mode(UINT adapter, D3DDISPLAYMODE *mode);
void get_fake_subscreen_display_mode_ex(UINT adapter, D3DDISPLAYMODEEX *mode);

HRESULT graphics_d3d9_gfdm_select_two_head_group_parameters(
        const D3DPRESENT_PARAMETERS *logical_presentation_parameters,
        const D3DDISPLAYMODEEX *logical_fullscreen_display_modes,
        D3DPRESENT_PARAMETERS *native_presentation_parameters,
        D3DDISPLAYMODEEX *native_fullscreen_display_modes,
        UINT *logical_small_swapchain,
        const char *operation);

HRESULT graphics_d3d9_gfdm_remap_two_head_group_parameters(
        D3DPRESENT_PARAMETERS *presentation_parameters,
        D3DDISPLAYMODEEX *fullscreen_display_modes,
        const char *operation);

void graphics_d3d9_gfdm_align_two_head_refresh_to_desktop(
        IDirect3D9Ex *d3d,
        UINT master_adapter,
        D3DPRESENT_PARAMETERS *presentation_parameters,
        D3DDISPLAYMODEEX *fullscreen_display_modes,
        const char *operation);

HRESULT validate_gfdm_two_head_exclusive(
        IDirect3D9 *d3d,
        UINT adapter,
        D3DDEVTYPE device_type,
        DWORD behavior_flags,
        const D3DPRESENT_PARAMETERS *presentation_parameters);

HRESULT graphics_d3d9_gfdm_recover_two_head_present_mode(
        IDirect3D9Ex *d3d,
        IDirect3DDevice9Ex *device,
        UINT master_adapter,
        const D3DPRESENT_PARAMETERS *desired_parameters,
        const D3DDISPLAYMODEEX *desired_modes);
