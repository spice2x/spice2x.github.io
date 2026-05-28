// dx11 factory vtable hooks. patches IDXGIFactory::CreateSwapChain and
// IDXGIFactory2::CreateSwapChainForHwnd so we can install_swapchain_hooks
// against every newly-created swapchain.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <atomic>
#include <mutex>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d11_internal.h"

namespace {

using CreateSwapChain_t = HRESULT(STDMETHODCALLTYPE *)(
    IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE *)(
    IDXGIFactory2 *, IUnknown *, HWND,
    const DXGI_SWAP_CHAIN_DESC1 *,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *,
    IDXGIOutput *, IDXGISwapChain1 **);

CreateSwapChain_t        CreateSwapChain_orig        = nullptr;
CreateSwapChainForHwnd_t CreateSwapChainForHwnd_orig = nullptr;

std::atomic<bool> g_factory_hooked  { false };
std::atomic<bool> g_factory2_hooked { false };
std::mutex g_hook_mutex;

HRESULT STDMETHODCALLTYPE CreateSwapChain_hook(
        IDXGIFactory *factory, IUnknown *pDevice,
        DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain)
{
    HRESULT res = CreateSwapChain_orig(factory, pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(res) && ppSwapChain && *ppSwapChain) {
        if (pDesc) {
            d3d11_hooks::note_main_hwnd(pDesc->OutputWindow);
        }
        d3d11_hooks::install_swapchain_hooks(*ppSwapChain);
    }
    return res;
}

HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd_hook(
        IDXGIFactory2 *factory, IUnknown *pDevice, HWND hWnd,
        const DXGI_SWAP_CHAIN_DESC1 *pDesc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
        IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain)
{
    HRESULT res = CreateSwapChainForHwnd_orig(
        factory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(res) && ppSwapChain && *ppSwapChain) {
        d3d11_hooks::note_main_hwnd(hWnd);
        d3d11_hooks::install_swapchain_hooks(*ppSwapChain);
    }
    return res;
}

} // namespace

namespace d3d11_hooks {

// patch IDXGIFactory::CreateSwapChain and (if implemented)
// IDXGIFactory2::CreateSwapChainForHwnd. idempotent.
void install_factory_hooks(IUnknown *factory) {
    if (!factory) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_hook_mutex);

    // IDXGIFactory::CreateSwapChain @ 10
    if (!g_factory_hooked) {
        IDXGIFactory *f = nullptr;
        if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&f))) && f) {
            if (hook_vtbl(f, 10, (void *) CreateSwapChain_hook,
                          (void **) &CreateSwapChain_orig,
                          "IDXGIFactory::CreateSwapChain")) {
                g_factory_hooked = true;
            }
            f->Release();
        }
    }

    // IDXGIFactory2::CreateSwapChainForHwnd @ 15
    if (!g_factory2_hooked) {
        IDXGIFactory2 *f2 = nullptr;
        if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&f2))) && f2) {
            if (hook_vtbl(f2, 15, (void *) CreateSwapChainForHwnd_hook,
                          (void **) &CreateSwapChainForHwnd_orig,
                          "IDXGIFactory2::CreateSwapChainForHwnd")) {
                g_factory2_hooked = true;
            }
            f2->Release();
        }
    }
}

}

#endif // SPICE_D3D11
