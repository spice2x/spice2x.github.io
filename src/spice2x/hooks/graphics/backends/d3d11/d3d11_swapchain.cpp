// dx11 swapchain vtable hooks + per-frame overlay pump.
//
// dxgi shares vtables across swapchain instances, so we only need to patch
// Present / Present1 / ResizeBuffers once on the first instance we see.
// each frame we lazily attach the overlay to whichever swapchain is
// presenting, then drive its imgui update / new_frame / render cycle.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <atomic>
#include <mutex>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d11_internal.h"

#include "external/imgui/imgui.h"
#include "external/imgui/backends/imgui_impl_dx11.h"
#include "overlay/imgui/impl_spice.h"

#include "hooks/graphics/graphics.h"
#include "util/utils.h"

// --------------------------------------------------------------------------
// overlay render bridge

namespace overlay::d3d11 {

    // sRGB backbuffers need a UNORM view: ImGui vertex colors are already
    // sRGB-encoded, so an extra linear->sRGB conversion would wash the
    // overlay out white.
    static DXGI_FORMAT to_unorm_view(DXGI_FORMAT fmt) {
        switch (fmt) {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
            default:                              return fmt;
        }
    }

    static void ensure_rtv(ID3D11Device *device,
                           IDXGISwapChain *swapchain,
                           ID3D11RenderTargetView **rtv)
    {
        if (*rtv || !device || !swapchain) {
            return;
        }
        ID3D11Texture2D *backbuffer = nullptr;
        if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || !backbuffer) {
            return;
        }
        D3D11_TEXTURE2D_DESC td {};
        backbuffer->GetDesc(&td);
        const DXGI_FORMAT view_fmt = to_unorm_view(td.Format);
        if (view_fmt != td.Format) {
            D3D11_RENDER_TARGET_VIEW_DESC rtvd {};
            rtvd.Format = view_fmt;
            rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(backbuffer, &rtvd, rtv);
        } else {
            device->CreateRenderTargetView(backbuffer, nullptr, rtv);
        }
        backbuffer->Release();
    }

    // bind the backbuffer (lazily creating the RTV) and draw the imgui
    // frame on top. reset_invalidate releases *rtv on ResizeBuffers.
    void render(ID3D11Device *device,
                ID3D11DeviceContext *context,
                IDXGISwapChain *swapchain,
                ID3D11RenderTargetView **rtv)
    {
        ensure_rtv(device, swapchain, rtv);
        if (!*rtv || !context) {
            return;
        }
        // present happens immediately after, so no need to save the previous
        // RT binding (flip-model resets it anyway).
        context->OMSetRenderTargets(1, rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

}

// --------------------------------------------------------------------------
// file-local state + per-frame helpers

namespace {

using Present_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain *, UINT, UINT);
using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Present1_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);

Present_t Present_orig = nullptr;
ResizeBuffers_t ResizeBuffers_orig = nullptr;
Present1_t Present1_orig = nullptr;

bool g_swapchain_hooked = false;
bool g_swapchain1_hooked = false;

// sub-screens / IME helpers are usually child or zero-sized windows.
// visibility isn't checked - the game may present before showing the window.
bool looks_like_game_window(HWND hwnd) {
    RECT client {};
    return GetAncestor(hwnd, GA_ROOT) == hwnd
        && GetClientRect(hwnd, &client)
        && client.right > client.left
        && client.bottom > client.top;
}

// only the main game window; ignore sub-screens / IME helpers.
bool is_main_game_swapchain(IDXGISwapChain *swapchain) {
    DXGI_SWAP_CHAIN_DESC desc {};
    if (!swapchain || FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow) {
        return false;
    }

    HWND main = d3d11_hooks::main_hwnd();
    if (!main) {
        // no creation hook recorded a window, so fall back to the presenting one;
        // the choice is permanent, so require a plausible game window
        if (!looks_like_game_window(desc.OutputWindow)) {
            return false;
        }

        log_misc(
            "graphics::d3d11",
            "try to notemain hwnd from swapchain present: 0x{:x}",
            (uintptr_t)desc.OutputWindow);

        d3d11_hooks::note_main_hwnd(desc.OutputWindow);

        // it may have been ignored, or another thread may have won the slot
        main = d3d11_hooks::main_hwnd();
    }
    return desc.OutputWindow == main;
}

// checks are ordered cheapest first, since this runs on every present
void try_create_overlay(IDXGISwapChain *swapchain) {
    if (!swapchain) {
        return;
    }

    // overlay is disabled by user
    if (!overlay::ENABLED) {
        return;
    }

    // overlay is already enabled and attached
    if (overlay::OVERLAY) {
        return;
    }

    // ignore sub windows
    if (!is_main_game_swapchain(swapchain)) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc {};
    if (FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow) {
        return;
    }

    // theme the native title bar; first present is the only reliable point for
    // windows whose swapchain bypasses our factory hooks (e.g. UnityPlayer.dll)
    set_window_dark_titlebar(desc.OutputWindow);

    ID3D11Device *device = nullptr;
    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) || !device) {
        return;
    }
    ID3D11DeviceContext *context = nullptr;
    device->GetImmediateContext(&context);

    if (context) {
        overlay::create_d3d11(desc.OutputWindow, device, context, swapchain);
        RECT cr {};
        ::GetClientRect(desc.OutputWindow, &cr);
        log_info("graphics::d3d11",
            "attached overlay to swapchain hwnd=0x{:x} backbuffer={}x{} client={}x{}",
            (uintptr_t) desc.OutputWindow,
            desc.BufferDesc.Width, desc.BufferDesc.Height,
            cr.right - cr.left, cr.bottom - cr.top);
        context->Release();
    }
    device->Release();
}

// screenshots have to keep working with the overlay disabled, so they are not gated on it
void pump_frame(IDXGISwapChain *swapchain) {
    const bool has_overlay =
        overlay::OVERLAY && overlay::OVERLAY->uses_swapchain(swapchain);
    if (!has_overlay && !is_main_game_swapchain(swapchain)) {
        return;
    }

    graphics_poll_screenshot_hotkey();

    // before the overlay render so the screenshot excludes it
    if (!GRAPHICS_SCREENSHOT_INCLUDE_OVERLAY) {
        d3d11_hooks::try_screenshot(swapchain);
    }

    if (has_overlay) {

        // size imgui to the backbuffer (not window client). dxgi may upscale
        // a small backbuffer into a larger client rect; without this override
        // imgui would draw past the RTV and the mouse mapping would be off.
        DXGI_SWAP_CHAIN_DESC desc {};
        if (SUCCEEDED(swapchain->GetDesc(&desc))) {
            ImGui_ImplSpice_SetDisplaySizeOverride(
                (float) desc.BufferDesc.Width,
                (float) desc.BufferDesc.Height);
        }

        overlay::OVERLAY->update();
        overlay::OVERLAY->new_frame();
        overlay::OVERLAY->render();
    }

    // after the overlay render so the screenshot includes toasts / menus
    if (GRAPHICS_SCREENSHOT_INCLUDE_OVERLAY) {
        d3d11_hooks::try_screenshot(swapchain);
    }
}

// ----------------------------------------------------------------------
// swapchain method hooks

HRESULT STDMETHODCALLTYPE Present_hook(
        IDXGISwapChain *swapchain, UINT SyncInterval, UINT Flags)
{
    // a test present doesn't display anything; don't pick a window or take a screenshot off it
    if (!(Flags & DXGI_PRESENT_TEST)) {
        try_create_overlay(swapchain);
        pump_frame(swapchain);
    }
    return Present_orig(swapchain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE Present1_hook(
        IDXGISwapChain1 *swapchain, UINT SyncInterval, UINT Flags,
        const DXGI_PRESENT_PARAMETERS *pParams)
{
    if (!(Flags & DXGI_PRESENT_TEST)) {
        try_create_overlay(swapchain);
        pump_frame(swapchain);
    }
    return Present1_orig(swapchain, SyncInterval, Flags, pParams);
}

HRESULT STDMETHODCALLTYPE ResizeBuffers_hook(
        IDXGISwapChain *swapchain, UINT BufferCount, UINT Width, UINT Height,
        DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    const bool ours = overlay::OVERLAY && overlay::OVERLAY->uses_swapchain(swapchain);
    if (ours) {
        log_info("graphics::d3d11", "ResizeBuffers {}x{} fmt={}",
            Width, Height, (int32_t) NewFormat);
        overlay::OVERLAY->reset_invalidate();
    }
    HRESULT res = ResizeBuffers_orig(
        swapchain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (ours && SUCCEEDED(res)) {
        overlay::OVERLAY->reset_recreate();
    }
    return res;
}

} // namespace

// --------------------------------------------------------------------------
// d3d11_hooks public surface: main-window tracking + vtable install.

namespace d3d11_hooks {

namespace {
    std::atomic<HWND> g_main_hwnd { nullptr };
    std::atomic<HWND> g_ignored_hwnd { nullptr };
}

void note_main_hwnd(HWND hwnd) {
    if (!hwnd || hwnd == g_ignored_hwnd.load()) {
        return;
    }
    HWND expected = nullptr;
    if (g_main_hwnd.compare_exchange_strong(expected, hwnd)) {
        log_info("graphics::d3d11", "main hwnd recorded: 0x{:x}",
            (uintptr_t) hwnd);
    }
}

HWND main_hwnd() {
    return g_main_hwnd.load();
}

void ignore_hwnd(HWND hwnd) {
    g_ignored_hwnd.store(hwnd);
}

// patch IDXGISwapChain::Present + ResizeBuffers and (if implemented)
// IDXGISwapChain1::Present1. idempotent; flag is set only after success
// so failed attempts can be retried on the next swapchain.
void install_swapchain_hooks(IDXGISwapChain *swapchain) {
    if (!swapchain) {
        return;
    }
    static std::mutex s_hook_mutex;
    std::lock_guard<std::mutex> lock(s_hook_mutex);

    if (!g_swapchain_hooked) {
        const bool a = hook_vtbl(swapchain, 8,  (void *) Present_hook,
                  (void **) &Present_orig,       "IDXGISwapChain::Present");
        const bool b = hook_vtbl(swapchain, 13, (void *) ResizeBuffers_hook,
                  (void **) &ResizeBuffers_orig, "IDXGISwapChain::ResizeBuffers");
        if (a && b) {
            g_swapchain_hooked = true;
        }
    }

    if (!g_swapchain1_hooked) {
        IDXGISwapChain1 *sc1 = nullptr;
        if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&sc1))) && sc1) {
            if (hook_vtbl(sc1, 22, (void *) Present1_hook,
                          (void **) &Present1_orig, "IDXGISwapChain1::Present1")) {
                g_swapchain1_hooked = true;
            }
            sc1->Release();
        }
    }
}

}

#endif // SPICE_D3D11
