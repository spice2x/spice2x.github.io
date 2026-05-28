#pragma once

// internal cross-file glue for the dx11 backend. all symbols are gated on
// SPICE_D3D11 (defined in overlay/overlay.h on 64-bit builds).

#include "overlay/overlay.h"

#ifdef SPICE_D3D11

#include "util/detour.h"
#include "util/logging.h"

struct HWND__; typedef HWND__ *HWND;
struct IUnknown;
struct IDXGISwapChain;

namespace d3d11_hooks {

    void install_swapchain_hooks(IDXGISwapChain *swapchain);
    void install_factory_hooks(IUnknown *factory);
    void try_capture_vtables();

    // remember the first non-null HWND associated with a game-created
    // swapchain. subsequent calls are ignored, so we always attach the
    // overlay to the main window (the first one the game made) rather than
    // a later sub-screen / IME helper / etc.
    void note_main_hwnd(HWND hwnd);
    HWND main_hwnd();

    // mark an HWND that note_main_hwnd should ignore (used for the dummy
    // window created during proactive vtable capture).
    void ignore_hwnd(HWND hwnd);

    // if a screenshot trigger is pending, copy the current backbuffer to
    // disk (and clipboard) as a PNG. no-op otherwise. call from the
    // present hook after the overlay has been rendered.
    void try_screenshot(IDXGISwapChain *swapchain);

    // trampoline a single virtual method on `iface`'s vtable. logs
    // success/failure under "graphics::d3d11". returns true on success.
    inline bool hook_vtbl(void *iface, size_t index,
                          void *hook, void **orig, const char *name)
    {
        void **vtbl = *reinterpret_cast<void ***>(iface);
        *orig = vtbl[index];
        if (!detour::trampoline_try(vtbl[index], hook, orig)) {
            log_warning("graphics::d3d11", "failed to hook {}", name);
            return false;
        }
        log_info("graphics::d3d11", "hooked {}", name);
        return true;
    }

}

#endif
