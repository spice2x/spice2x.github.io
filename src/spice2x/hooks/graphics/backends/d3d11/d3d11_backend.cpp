// dx11 / dxgi hooks for the spice2x imgui overlay.
//
// strategy: trampoline the relevant IDXGISwapChain / IDXGIFactory virtual
// methods the first time we see an instance. the vtable entries are shared
// across all instances, so one hook per method catches every swapchain.
// implementations of the vtable patches live in:
//   d3d11_swapchain.cpp      - Present / Present1 / ResizeBuffers
//   d3d11_factory.cpp        - CreateSwapChain[ForHwnd]
//   d3d11_vtable_capture.cpp - proactive capture via a dummy swapchain
//
// this file owns: init entrypoint, dxgi/d3d11 export-level trampolines,
// LDR DLL notification, and the fallback poll thread.
//
// lazy init is required for konami's `execexe` loader: it has its own
// preload step for d3d11.dll / dxgi.dll and fails (error 0xa) if those
// DLLs are already in the loader's module list. we therefore never call
// LoadLibrary on them and only touch their exports once they appear on
// their own.
//
// 64-bit only.

#include "d3d11_backend.h"

#ifndef SPICE_D3D11

void graphics_d3d11_init() {}

#else

#include <atomic>
#include <thread>
#include <chrono>
#include <cwchar>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d11_internal.h"
#include "util/nt_loader.h"

namespace {

using D3D11CreateDeviceAndSwapChain_t = HRESULT(WINAPI *)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
    ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
using CreateDXGIFactory_t  = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory1_t = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory2_t = HRESULT(WINAPI *)(UINT, REFIID, void **);

D3D11CreateDeviceAndSwapChain_t D3D11CreateDeviceAndSwapChain_orig = nullptr;
CreateDXGIFactory_t             CreateDXGIFactory_orig             = nullptr;
CreateDXGIFactory1_t            CreateDXGIFactory1_orig            = nullptr;
CreateDXGIFactory2_t            CreateDXGIFactory2_orig            = nullptr;

std::atomic<bool> g_d3d11_exports_hooked { false };
std::atomic<bool> g_dxgi_exports_hooked  { false };

// ----------------------------------------------------------------------
// top-level export hooks

HRESULT WINAPI D3D11CreateDeviceAndSwapChain_hook(
        IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
        const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
        const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain,
        ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel,
        ID3D11DeviceContext **ppImmediateContext)
{
    HRESULT res = D3D11CreateDeviceAndSwapChain_orig(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    if (SUCCEEDED(res) && ppSwapChain && *ppSwapChain) {
        if (pSwapChainDesc) {
            d3d11_hooks::note_main_hwnd(pSwapChainDesc->OutputWindow);
        }
        d3d11_hooks::install_swapchain_hooks(*ppSwapChain);
    }
    return res;
}

#define DEFINE_FACTORY_HOOK(NAME, SIG_PARAMS, ORIG_ARGS)                       \
    HRESULT WINAPI NAME##_hook SIG_PARAMS {                                    \
        HRESULT res = NAME##_orig ORIG_ARGS;                                   \
        if (SUCCEEDED(res) && ppFactory && *ppFactory) {                       \
            d3d11_hooks::install_factory_hooks(                                \
                reinterpret_cast<IUnknown *>(*ppFactory));                     \
        }                                                                      \
        return res;                                                            \
    }

DEFINE_FACTORY_HOOK(CreateDXGIFactory,
    (REFIID riid, void **ppFactory),
    (riid, ppFactory))
DEFINE_FACTORY_HOOK(CreateDXGIFactory1,
    (REFIID riid, void **ppFactory),
    (riid, ppFactory))
DEFINE_FACTORY_HOOK(CreateDXGIFactory2,
    (UINT Flags, REFIID riid, void **ppFactory),
    (Flags, riid, ppFactory))

#undef DEFINE_FACTORY_HOOK

// ----------------------------------------------------------------------
// export trampoline plumbing

// trampoline a DLL export if the DLL is loaded; never calls LoadLibrary.
// returns true on success.
bool trampoline_export(const char *dll, const char *name, void *hook, void **orig) {
    HMODULE mod = GetModuleHandleA(dll);
    if (!mod || *orig) {
        return *orig != nullptr;
    }
    void *addr = reinterpret_cast<void *>(GetProcAddress(mod, name));
    if (!addr) {
        return false;
    }
    *orig = addr;
    if (!detour::trampoline_try(addr, hook, orig)) {
        *orig = nullptr;
        return false;
    }
    log_info("graphics::d3d11", "trampolined {}!{}", dll, name);
    return true;
}

void try_install_d3d11_exports() {
    if (g_d3d11_exports_hooked) {
        return;
    }
    if (trampoline_export("d3d11.dll", "D3D11CreateDeviceAndSwapChain",
            (void *) D3D11CreateDeviceAndSwapChain_hook,
            (void **) &D3D11CreateDeviceAndSwapChain_orig)) {
        g_d3d11_exports_hooked = true;
    }
}

void try_install_dxgi_exports() {
    if (g_dxgi_exports_hooked) {
        return;
    }
    bool any = false;
    any |= trampoline_export("dxgi.dll", "CreateDXGIFactory",
            (void *) CreateDXGIFactory_hook,  (void **) &CreateDXGIFactory_orig);
    any |= trampoline_export("dxgi.dll", "CreateDXGIFactory1",
            (void *) CreateDXGIFactory1_hook, (void **) &CreateDXGIFactory1_orig);
    any |= trampoline_export("dxgi.dll", "CreateDXGIFactory2",
            (void *) CreateDXGIFactory2_hook, (void **) &CreateDXGIFactory2_orig);
    if (any) {
        g_dxgi_exports_hooked = true;
    }
}

void try_capture_if_ready() {
    if (g_d3d11_exports_hooked && g_dxgi_exports_hooked) {
        d3d11_hooks::try_capture_vtables();
    }
}

// ----------------------------------------------------------------------
// LDR notification + polling fallback

bool dll_name_matches(PCUNICODE_STRING name, const wchar_t *suffix) {
    if (!name || !name->Buffer) {
        return false;
    }
    size_t name_chars = name->Length / sizeof(WCHAR);
    size_t suffix_len = wcslen(suffix);
    return name_chars >= suffix_len
        && _wcsnicmp(name->Buffer + name_chars - suffix_len, suffix, suffix_len) == 0;
}

VOID CALLBACK ldr_dll_notification(
        ULONG reason, PCLDR_DLL_NOTIFICATION_DATA data, PVOID /*context*/)
{
    if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !data) {
        return;
    }
    if (dll_name_matches(data->Loaded.BaseDllName, L"d3d11.dll")) {
        try_install_d3d11_exports();
    } else if (dll_name_matches(data->Loaded.BaseDllName, L"dxgi.dll")) {
        try_install_dxgi_exports();
    }
}

// fallback for when execexe maps d3d11/dxgi via a path that bypasses
// LdrLoadDll (the notification then never fires). polls until both DLLs
// are trampolined or the timeout elapses.
void poll_thread() {
    using namespace std::chrono_literals;
    for (int32_t i = 0; i < 120; ++i) {
        try_install_d3d11_exports();
        try_install_dxgi_exports();
        if (g_d3d11_exports_hooked && g_dxgi_exports_hooked) {
            d3d11_hooks::try_capture_vtables();
            return;
        }
        std::this_thread::sleep_for(1s);
    }
}

} // namespace

void graphics_d3d11_init() {
    log_info("graphics::d3d11", "initializing");

    // trampoline now if either DLL is already in the PEB.
    try_install_d3d11_exports();
    try_install_dxgi_exports();
    try_capture_if_ready();

    // notification fires on standard LdrLoadDll paths.
    auto reg = reinterpret_cast<decltype(&LdrRegisterDllNotification)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
    if (reg) {
        static PVOID cookie = nullptr;
        NTSTATUS st = reg(0, ldr_dll_notification, nullptr, &cookie);
        if (NT_SUCCESS(st)) {
            log_info("graphics::d3d11", "registered LDR DLL notification");
        } else {
            log_warning("graphics::d3d11",
                "LdrRegisterDllNotification failed: {:#x}", (unsigned long)st);
        }
    }

    // polling fallback is only needed for execexe-based games (unity loader
    // manually maps d3d11/dxgi, bypassing LdrLoadDll). skip it otherwise to
    // avoid waking up every second on pure dx9 titles.
    if (GetModuleHandleW(L"execexe.dll")) {
        std::thread(poll_thread).detach();
    }
}

#endif // SPICE_D3D11
