// dx11 factory vtable hooks. patches CreateSwapChain / CreateSwapChainForHwnd
// so we can install_swapchain_hooks against every newly-created swapchain.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

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

CreateSwapChain_t CreateSwapChain_orig = nullptr;
CreateSwapChainForHwnd_t CreateSwapChainForHwnd_orig = nullptr;

bool g_factory_hooked = false;
bool g_factory2_hooked = false;
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

// QI-and-hook helper: dedupes the IDXGIFactory / IDXGIFactory2 install paths.
template<typename Iface>
void install_on(IUnknown *factory, bool &flag,
                size_t vtbl_index, void *hook, void **orig, const char *name)
{
    if (flag) {
        return;
    }
    Iface *f = nullptr;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&f))) || !f) {
        return;
    }
    if (d3d11_hooks::hook_vtbl(f, vtbl_index, hook, orig, name)) {
        flag = true;
    }
    f->Release();
}

} // namespace

namespace d3d11_hooks {

void install_factory_hooks(IUnknown *factory) {
    if (!factory) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_hook_mutex);

    install_on<IDXGIFactory>(factory, g_factory_hooked, 10,
        (void *) CreateSwapChain_hook, (void **) &CreateSwapChain_orig,
        "IDXGIFactory::CreateSwapChain");

    install_on<IDXGIFactory2>(factory, g_factory2_hooked, 15,
        (void *) CreateSwapChainForHwnd_hook, (void **) &CreateSwapChainForHwnd_orig,
        "IDXGIFactory2::CreateSwapChainForHwnd");
}

}

#endif // SPICE_D3D11
