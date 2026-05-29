// dx11 / dxgi hook entrypoint. trampolines d3d11.dll / dxgi.dll exports
// the moment those DLLs appear (LDR notification + poll-thread fallback),
// then drives proactive vtable capture so we don't lose the race against
// the execexe loader. per-vtable hook implementations live in the sibling
// files (d3d11_swapchain / d3d11_factory / d3d11_vtable_capture /
// d3d11_screenshot).
//
// note: never LoadLibrary d3d11/dxgi -- execexe pre-loads them itself and
// fails (error 0xa) if they're already in the loader's module list.
//
// 64-bit only.

#include "d3d11_backend.h"

#ifndef SPICE_D3D11

void graphics_d3d11_init() {}
void graphics_d3d11_shutdown() {}

#else

#include <atomic>
#include <thread>
#include <chrono>
#include <cwchar>
#include <mutex>

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
using CreateDXGIFactory_t = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory1_t = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory2_t = HRESULT(WINAPI *)(UINT, REFIID, void **);

D3D11CreateDeviceAndSwapChain_t D3D11CreateDeviceAndSwapChain_orig = nullptr;
CreateDXGIFactory_t CreateDXGIFactory_orig = nullptr;
CreateDXGIFactory1_t CreateDXGIFactory1_orig = nullptr;
CreateDXGIFactory2_t CreateDXGIFactory2_orig = nullptr;

std::atomic<bool> g_d3d11_exports_hooked { false };
std::atomic<bool> g_dxgi_exports_hooked { false };

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

// serializes trampoline_export() so the LDR notification callback and the
// poll thread don't race each other into MinHook against the same target.
std::mutex g_export_mutex;

bool trampoline_export(const char *dll, const char *name, void *hook, void **orig) {
    std::lock_guard<std::mutex> lock(g_export_mutex);
    if (*orig) {
        return true;
    }
    HMODULE mod = GetModuleHandleA(dll);
    if (!mod) {
        return false;
    }
    void *addr = reinterpret_cast<void *>(GetProcAddress(mod, name));
    if (!addr) {
        return false;
    }
    *orig = addr; // trampoline_try reads *orig before overwriting it.
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
    struct entry { const char *name; void *hook; void **orig; };
    const entry entries[] = {
        { "CreateDXGIFactory", (void *) CreateDXGIFactory_hook,
          (void **) &CreateDXGIFactory_orig },
        { "CreateDXGIFactory1", (void *) CreateDXGIFactory1_hook,
          (void **) &CreateDXGIFactory1_orig },
        { "CreateDXGIFactory2", (void *) CreateDXGIFactory2_hook,
          (void **) &CreateDXGIFactory2_orig },
    };
    bool any = false;
    for (auto &e : entries) {
        any |= trampoline_export("dxgi.dll", e.name, e.hook, e.orig);
    }
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

bool dll_name_ends_with(PCUNICODE_STRING name, const wchar_t *suffix) {
    if (!name || !name->Buffer) {
        return false;
    }
    const size_t n = name->Length / sizeof(WCHAR);
    const size_t s = wcslen(suffix);
    return n >= s && _wcsnicmp(name->Buffer + n - s, suffix, s) == 0;
}

VOID CALLBACK ldr_dll_notification(
        ULONG reason, PCLDR_DLL_NOTIFICATION_DATA data, PVOID /*context*/)
{
    if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !data) {
        return;
    }
    if (dll_name_ends_with(data->Loaded.BaseDllName, L"d3d11.dll")) {
        try_install_d3d11_exports();
    } else if (dll_name_ends_with(data->Loaded.BaseDllName, L"dxgi.dll")) {
        try_install_dxgi_exports();
    }
}

// execexe maps d3d11/dxgi via a path that bypasses LdrLoadDll, so the
// notification above never fires for those DLLs and we have to poll.
std::atomic<bool> g_stop { false };
std::thread g_poll_thread;
std::mutex g_init_mutex;
PVOID g_ldr_cookie = nullptr;

void poll_thread() {
    using namespace std::chrono_literals;
    for (int32_t i = 0; i < 120 && !g_stop.load(); ++i) {
        try_install_d3d11_exports();
        try_install_dxgi_exports();
        if (g_d3d11_exports_hooked && g_dxgi_exports_hooked) {
            d3d11_hooks::try_capture_vtables();
            return;
        }
        // sliced so shutdown doesn't have to wait a full second.
        for (int32_t s = 0; s < 10 && !g_stop.load(); ++s) {
            std::this_thread::sleep_for(100ms);
        }
    }
}

} // namespace

void graphics_d3d11_init() {
    // dx11 titles always run under execexe. skipping on pure-dx9 games keeps
    // their startup path completely untouched (no exports patched, no poll
    // thread, no LDR callback).
    if (!GetModuleHandleW(L"execexe.dll")) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_poll_thread.joinable()) {
        return; // already initialized
    }

    log_info("graphics::d3d11", "initializing");

    // trampoline now if either DLL is already in the PEB.
    try_install_d3d11_exports();
    try_install_dxgi_exports();
    try_capture_if_ready();

    // catches standard LdrLoadDll loads.
    auto reg = reinterpret_cast<decltype(&LdrRegisterDllNotification)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
    if (reg) {
        NTSTATUS st = reg(0, ldr_dll_notification, nullptr, &g_ldr_cookie);
        if (NT_SUCCESS(st)) {
            log_info("graphics::d3d11", "registered LDR DLL notification");
        } else {
            g_ldr_cookie = nullptr;
            log_warning("graphics::d3d11",
                "LdrRegisterDllNotification failed: {:#x}", (unsigned long)st);
        }
    }

    // catches the execexe loader path that bypasses LdrLoadDll.
    g_poll_thread = std::thread(poll_thread);
}

void graphics_d3d11_shutdown() {
    std::lock_guard<std::mutex> lock(g_init_mutex);

    // unregister first so the callback can't fire mid-teardown.
    if (g_ldr_cookie) {
        auto unreg = reinterpret_cast<decltype(&LdrUnregisterDllNotification)>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrUnregisterDllNotification"));
        if (unreg) {
            unreg(g_ldr_cookie);
        }
        g_ldr_cookie = nullptr;
    }

    g_stop.store(true);
    if (g_poll_thread.joinable()) {
        g_poll_thread.join();
    }
}

#endif // SPICE_D3D11
