// DANCE aROUND window attachment, frame pacing and display-mode overrides.
#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <chrono>
#include <cstdint>
#include <mutex>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "d3d11_internal.h"
#include "d3d11_swapchain_udn.h"
#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "util/precise_timer.h"

namespace d3d11_hooks::udn {
namespace {

using SetFullscreenState_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain *, BOOL, IDXGIOutput *);
using ResizeTarget_t = HRESULT(STDMETHODCALLTYPE *)(
        IDXGISwapChain *, const DXGI_MODE_DESC *);

SetFullscreenState_t SetFullscreenState_orig = nullptr;
ResizeTarget_t ResizeTarget_orig = nullptr;

bool g_set_fullscreen_hooked = false;
bool g_resize_target_hooked = false;
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

void schedule_window_attach(IDXGISwapChain *swapchain) {
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

void apply_present_policy(IDXGISwapChain *swapchain, UINT &sync_interval) {
    if (!GRAPHICS_WINDOWED || !avs::game::is_model("UDN") ||
        !is_main_game_swapchain(swapchain)) {
        return;
    }
    // Interval two gives 60 FPS on the cabinet's 120 Hz display, but only
    // 37.5 FPS on a 75 Hz desktop. Pace windowed mode with an explicit clock.
    // Fullscreen keeps the game's own presentation interval and flags.
    pace_forced_present(60);
    sync_interval = 0;
}

void install_swapchain_hooks(IDXGISwapChain *swapchain) {
    if (!GRAPHICS_WINDOWED || !avs::game::is_model("UDN")) {
        return;
    }
    // The shared installer holds its mutex while calling this function.
    if (!g_set_fullscreen_hooked) {
        g_set_fullscreen_hooked = hook_vtbl(
            swapchain, 10, (void *) SetFullscreenState_hook,
            (void **) &SetFullscreenState_orig,
            "IDXGISwapChain::SetFullscreenState");
    }
    if (!g_resize_target_hooked) {
        g_resize_target_hooked = hook_vtbl(
            swapchain, 14, (void *) ResizeTarget_hook,
            (void **) &ResizeTarget_orig, "IDXGISwapChain::ResizeTarget");
    }
}

} // namespace d3d11_hooks::udn

#endif // SPICE_D3D11
