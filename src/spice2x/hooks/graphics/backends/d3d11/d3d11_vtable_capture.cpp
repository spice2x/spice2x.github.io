// proactive vtable capture for the dx11 backend.
//
// without this, unity titles loaded through execexe loader routinely race past
// our export-level trampolines: by the time we patch CreateDXGIFactory* /
// D3D11CreateDevice the game has already produced its real swapchain and we
// never see Present.
//
// we work around it by creating a throwaway device + swapchain ourselves
// the moment d3d11.dll + dxgi.dll appear, which lets us patch the shared
// IDXGISwapChain[1] / IDXGIFactory[2] vtables before the game's first
// swapchain is created.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <atomic>
#include <memory>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d11_internal.h"

namespace {

using D3D11CreateDevice_t = HRESULT(WINAPI *)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT,
    ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
using CreateDXGIFactory1_t = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory2_t = HRESULT(WINAPI *)(UINT, REFIID, void **);

std::atomic<bool> g_vtables_captured { false };

// minimal COM RAII so the capture path doesn't drown in cleanup branches.
struct com_release {
    void operator()(IUnknown *p) const {
        if (p) {
            p->Release();
        }
    }
};
template<typename T> using com_ptr = std::unique_ptr<T, com_release>;

// resolve via GetProcAddress on an already-loaded module; never LoadLibrary.
template<typename Fn>
Fn resolve(HMODULE mod, const char *name) {
    return reinterpret_cast<Fn>(GetProcAddress(mod, name));
}

com_ptr<IDXGIFactory2> create_factory2(CreateDXGIFactory2_t f2,
                                       CreateDXGIFactory1_t f1)
{
    IDXGIFactory2 *raw = nullptr;
    if (f2 && SUCCEEDED(f2(0, IID_PPV_ARGS(&raw))) && raw) {
        return com_ptr<IDXGIFactory2>(raw);
    }
    IDXGIFactory1 *factory1 = nullptr;
    if (f1 && SUCCEEDED(f1(IID_PPV_ARGS(&factory1))) && factory1) {
        factory1->QueryInterface(IID_PPV_ARGS(&raw));
        factory1->Release();
    }
    return com_ptr<IDXGIFactory2>(raw);
}

bool create_dummy_device(D3D11CreateDevice_t create,
                         com_ptr<ID3D11Device> &device,
                         com_ptr<ID3D11DeviceContext> &context)
{
    static constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    // try hardware first, then WARP so headless / unusual configs still work.
    for (auto type : { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP }) {
        ID3D11Device *d = nullptr;
        ID3D11DeviceContext *c = nullptr;
        D3D_FEATURE_LEVEL got;
        if (SUCCEEDED(create(nullptr, type, nullptr, 0,
                             levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                             &d, &got, &c)) && d) {
            device.reset(d);
            context.reset(c);
            return true;
        }
    }
    return false;
}

} // namespace

namespace d3d11_hooks {

// create a throwaway device + swapchain so we can patch the shared
// vtables before the game's loader races past our export-level
// trampolines. safe to call repeatedly; runs at most once.
void try_capture_vtables() {
    if (g_vtables_captured.load()) {
        return;
    }

    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    HMODULE dxgi  = GetModuleHandleW(L"dxgi.dll");
    if (!d3d11 || !dxgi) {
        return;
    }

    auto create_device = resolve<D3D11CreateDevice_t>(d3d11, "D3D11CreateDevice");
    auto f2 = resolve<CreateDXGIFactory2_t>(dxgi, "CreateDXGIFactory2");
    auto f1 = resolve<CreateDXGIFactory1_t>(dxgi, "CreateDXGIFactory1");
    if (!create_device || (!f1 && !f2)) {
        return;
    }

    if (g_vtables_captured.exchange(true)) {
        return;
    }

    // hidden message-only window. STATIC is always registered by user32 so
    // we don't need our own class.
    HWND dummy_hwnd = CreateWindowExW(
        0, L"STATIC", L"", 0, 0, 0, 1, 1,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dummy_hwnd) {
        log_warning("graphics::d3d11",
            "vtable capture: CreateWindowExW failed (gle={})", (unsigned long)GetLastError());
        return;
    }
    auto destroy_hwnd = std::unique_ptr<HWND__, decltype(&DestroyWindow)>(
        dummy_hwnd, &DestroyWindow);

    // if the game's CreateDXGIFactory_hook already raced us and installed
    // factory hooks, our CreateSwapChainForHwnd call below will trip the
    // hook and try to record dummy_hwnd as the main window. block that.
    ignore_hwnd(dummy_hwnd);

    auto factory2 = create_factory2(f2, f1);
    if (!factory2) {
        log_warning("graphics::d3d11", "vtable capture: CreateDXGIFactory* failed");
        return;
    }

    com_ptr<ID3D11Device>        device;
    com_ptr<ID3D11DeviceContext> context;
    if (!create_dummy_device(create_device, device, context)) {
        log_warning("graphics::d3d11", "vtable capture: D3D11CreateDevice failed");
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 desc {};
    desc.Width = 1;
    desc.Height = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1 *raw_sc = nullptr;
    HRESULT hr = factory2->CreateSwapChainForHwnd(
        device.get(), dummy_hwnd, &desc, nullptr, nullptr, &raw_sc);
    if (FAILED(hr) || !raw_sc) {
        log_warning("graphics::d3d11",
            "vtable capture: CreateSwapChainForHwnd failed (hr={:#x})", (unsigned long)hr);
        return;
    }
    com_ptr<IDXGISwapChain1> swapchain(raw_sc);

    // patch the shared vtables. all real swapchains/factories created later
    // will dispatch through our hooks.
    install_swapchain_hooks(swapchain.get());
    install_factory_hooks(factory2.get());

    log_info("graphics::d3d11", "vtable capture complete (via dummy swapchain)");
}

}

#endif // SPICE_D3D11
