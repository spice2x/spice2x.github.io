// dx11 swapchain vtable hooks + per-frame overlay pump.
//
// the Present / Present1 / ResizeBuffers entries are patched on the first
// swapchain instance we see; dxgi shares vtables across instances so the
// patch catches every subsequent swapchain too. each frame we lazily attach
// the overlay to whichever swapchain is presenting, then drive its imgui
// update / new_frame / render cycle.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <atomic>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d11_internal.h"

#include "external/imgui/imgui.h"
#include "external/imgui/backends/imgui_impl_dx11.h"
#include "overlay/imgui/impl_spice.h"

#include "games/io.h"
#include "hooks/graphics/graphics.h"
#include "launcher/launcher.h"
#include "misc/eamuse.h"

// --------------------------------------------------------------------------
// overlay render bridge

namespace overlay::d3d11 {

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

        // if the backbuffer is sRGB, create a UNORM view so ImGui's already
        // sRGB-encoded vertex colors are written through without an extra
        // linear->sRGB conversion (which would wash the overlay out white).
        D3D11_TEXTURE2D_DESC td {};
        backbuffer->GetDesc(&td);
        DXGI_FORMAT view_fmt = td.Format;
        switch (td.Format) {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                view_fmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                view_fmt = DXGI_FORMAT_B8G8R8A8_UNORM; break;
            default: break;
        }
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

    // binds the swapchain backbuffer as a render target (lazily creating
    // the RTV on first use) and draws the current imgui frame on top.
    // *rtv is created on demand and reused across frames;
    // reset_invalidate must release it on ResizeBuffers.
    void render(ID3D11Device *device,
                ID3D11DeviceContext *context,
                IDXGISwapChain *swapchain,
                ID3D11RenderTargetView **rtv)
    {
        ensure_rtv(device, swapchain, rtv);
        if (!*rtv || !context) {
            return;
        }
        // present happens immediately after, so we don't bother saving the
        // previous RT binding (flip-model resets it anyway).
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

Present_t       Present_orig       = nullptr;
ResizeBuffers_t ResizeBuffers_orig = nullptr;
Present1_t      Present1_orig      = nullptr;

std::atomic<bool> g_swapchain_hooked  { false };
std::atomic<bool> g_swapchain1_hooked { false };

void try_create_overlay(IDXGISwapChain *swapchain) {
    if (!swapchain || overlay::OVERLAY) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc {};
    if (FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow) {
        return;
    }

    // only attach to the main game window (first swapchain ever created).
    // ignores sub-screens, IME helper windows, etc.
    HWND main = d3d11_hooks::main_hwnd();
    if (main && desc.OutputWindow != main) {
        return;
    }

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

// poll the Screenshot overlay hotkey with rising-edge detection. d3d9 does
// this inline in its present hook; we mirror the gating (hotkeys-enabled or
// no overlay yet) so behaviour matches across backends.
static void poll_screenshot_hotkey() {
    static bool s_down = false;
    auto buttons = games::get_buttons_overlay(eamuse_get_game());
    const bool pressed = buttons
        && (!overlay::OVERLAY || overlay::OVERLAY->hotkeys_triggered())
        && GameAPI::Buttons::getState(RI_MGR,
                buttons->at(games::OverlayButtons::Screenshot));
    if (pressed) {
        if (!s_down) {
            graphics_screenshot_trigger();
        }
        s_down = true;
    } else {
        s_down = false;
    }
}

void pump_overlay(IDXGISwapChain *swapchain) {
    if (!overlay::OVERLAY || !overlay::OVERLAY->uses_swapchain(swapchain)) {
        return;
    }

    poll_screenshot_hotkey();

    // tell imgui to size its viewport to the backbuffer, not the window's
    // client rect. games commonly create a 1920x1080 backbuffer that dxgi
    // then upscales into a larger client area; without this override ImGui
    // would draw past the RTV and the mouse mapping would be off.
    DXGI_SWAP_CHAIN_DESC desc {};
    if (SUCCEEDED(swapchain->GetDesc(&desc))) {
        ImGui_ImplSpice_SetDisplaySizeOverride(
            (float) desc.BufferDesc.Width,
            (float) desc.BufferDesc.Height);
    }

    overlay::OVERLAY->update();
    overlay::OVERLAY->new_frame();
    overlay::OVERLAY->render();

    // capture screenshot if one was requested (after overlay render so
    // toasts/menus appear in the saved image, matching d3d9 behaviour).
    d3d11_hooks::try_screenshot(swapchain);
}

// ----------------------------------------------------------------------
// swapchain method hooks

HRESULT STDMETHODCALLTYPE Present_hook(
        IDXGISwapChain *swapchain, UINT SyncInterval, UINT Flags)
{
    try_create_overlay(swapchain);
    pump_overlay(swapchain);
    return Present_orig(swapchain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE Present1_hook(
        IDXGISwapChain1 *swapchain, UINT SyncInterval, UINT Flags,
        const DXGI_PRESENT_PARAMETERS *pParams)
{
    try_create_overlay(swapchain);
    pump_overlay(swapchain);
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
// install_swapchain_hooks: one-shot global vtable patch.

namespace d3d11_hooks {

// main-window tracking. set once on the first swapchain creation we see;
// later swapchains (sub-screens, IME helpers) are ignored.
namespace {
    std::atomic<HWND> g_main_hwnd    { nullptr };
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
// IDXGISwapChain1::Present1. idempotent across instances since dxgi
// shares a single vtable per interface.
void install_swapchain_hooks(IDXGISwapChain *swapchain) {
    if (!swapchain) {
        return;
    }

    // IDXGISwapChain: Present @ 8, ResizeBuffers @ 13.
    if (!g_swapchain_hooked.exchange(true)) {
        hook_vtbl(swapchain, 8,  (void *) Present_hook,
                  (void **) &Present_orig,       "IDXGISwapChain::Present");
        hook_vtbl(swapchain, 13, (void *) ResizeBuffers_hook,
                  (void **) &ResizeBuffers_orig, "IDXGISwapChain::ResizeBuffers");
    }

    // IDXGISwapChain1::Present1 @ 22 (DXGI 1.2, flip-model).
    if (!g_swapchain1_hooked) {
        IDXGISwapChain1 *sc1 = nullptr;
        if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&sc1))) && sc1) {
            if (!g_swapchain1_hooked.exchange(true)) {
                hook_vtbl(sc1, 22, (void *) Present1_hook,
                          (void **) &Present1_orig, "IDXGISwapChain1::Present1");
            }
            sc1->Release();
        }
    }
}

}

#endif // SPICE_D3D11
