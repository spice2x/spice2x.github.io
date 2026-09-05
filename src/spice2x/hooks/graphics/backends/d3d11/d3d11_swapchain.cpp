// dx11 swapchain vtable hooks + per-frame overlay pump.
//
// dxgi shares vtables across swapchain instances, so we only need to patch
// Present / Present1 / ResizeBuffers once on the first instance we see.
// each frame we lazily attach the overlay to whichever swapchain is
// presenting, then drive its imgui update / new_frame / render cycle.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d11_internal.h"

#include "avs/game.h"
#include "external/imgui/imgui.h"
#include "external/imgui/backends/imgui_impl_dx11.h"
#include "overlay/imgui/impl_spice.h"

#include "hooks/graphics/graphics.h"
#include "util/precise_timer.h"
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
using SetFullscreenState_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain *, BOOL, IDXGIOutput *);
using ResizeTarget_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain *, const DXGI_MODE_DESC *);

Present_t Present_orig = nullptr;
ResizeBuffers_t ResizeBuffers_orig = nullptr;
Present1_t Present1_orig = nullptr;
SetFullscreenState_t SetFullscreenState_orig = nullptr;
ResizeTarget_t ResizeTarget_orig = nullptr;

bool g_present_hooked = false;
bool g_resize_buffers_hooked = false;
bool g_set_fullscreen_hooked = false;
bool g_resize_target_hooked = false;
bool g_swapchain1_hooked = false;
constexpr UINT DXGI_PRESENT_ALLOW_TEARING_FLAG = 0x00000200u;
// Present and Present1 share one waitable timer and frame deadline.
std::mutex g_forced_present_pacing_mutex;
std::chrono::steady_clock::time_point g_next_forced_present {};

// Unity creates and owns its top-level window on a different thread from the
// D3D11 Present thread. Subclassing or resizing that HWND from Present can
// synchronously wait for the owner thread and deadlock Unity. A one-time
// message-queue hook performs graphics_hook_window on the HWND's owner thread.
std::mutex g_window_attach_mutex;
HHOOK g_window_attach_hook = nullptr;
HWND g_window_attach_target = nullptr;
bool g_window_attach_complete = false;

UINT window_attach_message() {
    static const UINT message = RegisterWindowMessageW(
            L"SpiceTools.D3D11.AttachGameWindow");
    return message;
}

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

void pace_forced_present(UINT frame_rate) {
    static timeutils::PreciseSleepTimer timer;
    const auto period = std::chrono::nanoseconds(
            1'000'000'000ULL / static_cast<uint64_t>(frame_rate));
    std::lock_guard<std::mutex> lock(g_forced_present_pacing_mutex);
    const auto now = std::chrono::steady_clock::now();
    if (g_next_forced_present.time_since_epoch().count() == 0 ||
        now > g_next_forced_present + period) {
        g_next_forced_present = now + period;
        return;
    }
    if (now < g_next_forced_present) {
        timer.sleep(g_next_forced_present - now);
    }
    g_next_forced_present += period;
}

void apply_udn_present_policy(
        IDXGISwapChain *swapchain, UINT &sync_interval, UINT &flags) {
    if (!avs::game::is_model("UDN") || !is_main_game_swapchain(swapchain)) {
        return;
    }
    if (GRAPHICS_WINDOWED) {
        // Interval two gives 60 FPS on the cabinet's 120 Hz display, but only
        // 37.5 FPS on a 75 Hz desktop. Pace windowed mode with an explicit clock.
        pace_forced_present(60);
        sync_interval = 0;
    } else {
        sync_interval = std::max(sync_interval, 2u);
    }
    flags &= ~DXGI_PRESENT_ALLOW_TEARING_FLAG;
}

LRESULT CALLBACK window_attach_getmessage(
        int code, WPARAM w_param, LPARAM l_param) {
    HWND target = nullptr;
    HHOOK hook = nullptr;
    if (code >= 0 && w_param == PM_REMOVE) {
        const auto message = reinterpret_cast<const MSG *>(l_param);
        std::lock_guard<std::mutex> lock(g_window_attach_mutex);
        if (!g_window_attach_complete && message->hwnd == g_window_attach_target &&
            message->message == window_attach_message()) {
            target = g_window_attach_target;
            hook = g_window_attach_hook;
            g_window_attach_hook = nullptr;
            g_window_attach_complete = true;
        }
    }

    const LRESULT result = CallNextHookEx(nullptr, code, w_param, l_param);
    if (target) {
        UnhookWindowsHookEx(hook);
        graphics_hook_window(target, nullptr);
        log_info(
            "graphics::d3d11",
            "attached Spice window hooks on owner thread for hwnd=0x{:x}",
            (uintptr_t) target);
    }
    return result;
}

void schedule_main_window_attach(IDXGISwapChain *swapchain) {
    if (!GRAPHICS_WINDOWED || !avs::game::is_model("UDN") ||
        !is_main_game_swapchain(swapchain)) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc {};
    if (FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow) {
        return;
    }
    const HWND hwnd = desc.OutputWindow;

    std::lock_guard<std::mutex> lock(g_window_attach_mutex);
    if (g_window_attach_complete || g_window_attach_hook != nullptr) {
        return;
    }
    if (GRAPHICS_HOOKED_WINDOW.has_value()) {
        g_window_attach_complete = true;
        return;
    }

    DWORD process_id = 0;
    const DWORD thread_id = GetWindowThreadProcessId(hwnd, &process_id);
    if (!thread_id || process_id != GetCurrentProcessId()) {
        log_warning(
            "graphics::d3d11",
            "could not identify owner thread for hwnd=0x{:x}",
            (uintptr_t) hwnd);
        return;
    }

    const HHOOK hook = SetWindowsHookExW(
            WH_GETMESSAGE, window_attach_getmessage, nullptr, thread_id);
    if (!hook) {
        log_warning(
            "graphics::d3d11",
            "could not schedule owner-thread window attach for hwnd=0x{:x}: {}",
            (uintptr_t) hwnd,
            GetLastError());
        return;
    }

    g_window_attach_target = hwnd;
    g_window_attach_hook = hook;
    if (!PostMessageW(hwnd, window_attach_message(), 0, 0)) {
        const DWORD error = GetLastError();
        g_window_attach_hook = nullptr;
        g_window_attach_target = nullptr;
        UnhookWindowsHookEx(hook);
        log_warning(
            "graphics::d3d11",
            "could not post owner-thread window attach for hwnd=0x{:x}: {}",
            (uintptr_t) hwnd,
            error);
        return;
    }

    log_info(
        "graphics::d3d11",
        "scheduled Spice window attach on owner thread {} for hwnd=0x{:x}",
        thread_id,
        (uintptr_t) hwnd);
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
        schedule_main_window_attach(swapchain);
        try_create_overlay(swapchain);
        pump_frame(swapchain);
        apply_udn_present_policy(swapchain, SyncInterval, Flags);
    }
    return Present_orig(swapchain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE Present1_hook(
        IDXGISwapChain1 *swapchain, UINT SyncInterval, UINT Flags,
        const DXGI_PRESENT_PARAMETERS *pParams)
{
    if (!(Flags & DXGI_PRESENT_TEST)) {
        schedule_main_window_attach(swapchain);
        try_create_overlay(swapchain);
        pump_frame(swapchain);
        apply_udn_present_policy(swapchain, SyncInterval, Flags);
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

HRESULT STDMETHODCALLTYPE SetFullscreenState_hook(
        IDXGISwapChain *swapchain, BOOL Fullscreen, IDXGIOutput *Target) {
    if (Fullscreen && GRAPHICS_WINDOWED && avs::game::is_model("UDN") &&
        is_main_game_swapchain(swapchain)) {
        log_info(
            "graphics::d3d11",
            "blocked UDN request to enter DXGI exclusive fullscreen");
        return SetFullscreenState_orig(swapchain, FALSE, nullptr);
    }
    return SetFullscreenState_orig(swapchain, Fullscreen, Target);
}

HRESULT STDMETHODCALLTYPE ResizeTarget_hook(
        IDXGISwapChain *swapchain, const DXGI_MODE_DESC *NewTargetParameters) {
    if (GRAPHICS_WINDOWED && avs::game::is_model("UDN") &&
        is_main_game_swapchain(swapchain)) {
        if (NewTargetParameters) {
            log_info(
                "graphics::d3d11",
                "blocked UDN output-mode resize to {}x{}",
                NewTargetParameters->Width,
                NewTargetParameters->Height);
        }
        return S_OK;
    }
    return ResizeTarget_orig(swapchain, NewTargetParameters);
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

// Patch the base swapchain methods and (if implemented) Present1. Each method
// has its own completion flag so a failed attempt can be retried without
// trying to hook an already-patched vtable target again.
void install_swapchain_hooks(IDXGISwapChain *swapchain) {
    if (!swapchain) {
        return;
    }
    static std::mutex s_hook_mutex;
    std::lock_guard<std::mutex> lock(s_hook_mutex);

    if (!g_present_hooked) {
        g_present_hooked = hook_vtbl(
            swapchain, 8, (void *) Present_hook,
            (void **) &Present_orig, "IDXGISwapChain::Present");
    }
    if (GRAPHICS_WINDOWED && avs::game::is_model("UDN") && !g_set_fullscreen_hooked) {
        g_set_fullscreen_hooked = hook_vtbl(
            swapchain, 10, (void *) SetFullscreenState_hook,
            (void **) &SetFullscreenState_orig,
            "IDXGISwapChain::SetFullscreenState");
    }
    if (!g_resize_buffers_hooked) {
        g_resize_buffers_hooked = hook_vtbl(
            swapchain, 13, (void *) ResizeBuffers_hook,
            (void **) &ResizeBuffers_orig, "IDXGISwapChain::ResizeBuffers");
    }
    if (GRAPHICS_WINDOWED && avs::game::is_model("UDN") && !g_resize_target_hooked) {
        g_resize_target_hooked = hook_vtbl(
            swapchain, 14, (void *) ResizeTarget_hook,
            (void **) &ResizeTarget_orig, "IDXGISwapChain::ResizeTarget");
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
