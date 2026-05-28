#pragma once

// internal glue for the dx11 backend. all symbols gated on SPICE_D3D11.

#include "overlay/overlay.h"

#ifdef SPICE_D3D11

#include <memory>

#include "util/detour.h"
#include "util/logging.h"

struct HWND__; typedef HWND__ *HWND;
struct IUnknown;
struct IDXGISwapChain;

namespace d3d11_hooks {

    void install_swapchain_hooks(IDXGISwapChain *swapchain);
    void install_factory_hooks(IUnknown *factory);
    void try_capture_vtables();

    // first non-null swapchain HWND wins; later ones (sub-screens, IME
    // helpers) are ignored. the dummy capture window is exempted via
    // ignore_hwnd.
    void note_main_hwnd(HWND hwnd);
    HWND main_hwnd();
    void ignore_hwnd(HWND hwnd);

    // capture backbuffer to PNG if a screenshot was requested.
    void try_screenshot(IDXGISwapChain *swapchain);

    // trampoline a virtual method by vtable index. on failure *orig is null.
    inline bool hook_vtbl(void *iface, size_t index,
                          void *hook, void **orig, const char *name)
    {
        void **vtbl = *reinterpret_cast<void ***>(iface);
        void *target = vtbl[index];
        // trampoline_try reads *orig before overwriting it.
        *orig = target;
        if (!detour::trampoline_try(target, hook, orig)) {
            *orig = nullptr;
            log_warning("graphics::d3d11", "failed to hook {}", name);
            return false;
        }
        log_info("graphics::d3d11", "hooked {}", name);
        return true;
    }

    // minimal COM RAII used by capture / screenshot paths.
    struct com_release {
        void operator()(IUnknown *p) const { if (p) p->Release(); }
    };
    template<typename T> using com_ptr = std::unique_ptr<T, com_release>;

}

#endif
