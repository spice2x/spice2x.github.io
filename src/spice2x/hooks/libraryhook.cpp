#include "libraryhook.h"

#include "external/robin_hood.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/utils.h"

static bool LHOOK_ENABLED = false;
static robin_hood::unordered_map<std::string, HMODULE> LIBRARIES_A;
static robin_hood::unordered_map<std::wstring, HMODULE> LIBRARIES_W;
static robin_hood::unordered_map<std::string, FARPROC> PROCS;

static decltype(LoadLibraryA) *LoadLibraryA_orig = nullptr;
static decltype(LoadLibraryW) *LoadLibraryW_orig = nullptr;
static decltype(GetModuleHandleA) *GetModuleHandleA_orig = nullptr;
static decltype(GetModuleHandleW) *GetModuleHandleW_orig = nullptr;
static decltype(GetProcAddress) *GetProcAddress_orig = nullptr;

static HMODULE WINAPI LoadLibraryA_hook(LPCTSTR lpFileName) {

    // check hooks
    if (lpFileName) {
        auto module = LIBRARIES_A.find(lpFileName);
        if (module != LIBRARIES_A.end()) {
            return module->second;
        }
    }

    // fallback
    return LoadLibraryA_orig(lpFileName);
}

static HMODULE WINAPI LoadLibraryW_hook(LPCWSTR lpFileName) {

    // check hooks
    if (lpFileName) {
        auto module = LIBRARIES_W.find(lpFileName);
        if (module != LIBRARIES_W.end()) {
            return module->second;
        }
    }

    // fallback
    return LoadLibraryW_orig(lpFileName);
}

static HMODULE WINAPI GetModuleHandleA_hook(LPCSTR lpModuleName) {

    // check hooks
    if (lpModuleName) {
        auto module = LIBRARIES_A.find(lpModuleName);
        if (module != LIBRARIES_A.end()) {
            return module->second;
        }
    }

    // fallback
    return GetModuleHandleA_orig(lpModuleName);
}

static HMODULE WINAPI GetModuleHandleW_hook(LPCWSTR lpModuleName) {

    // check hooks
    if (lpModuleName) {
        auto module = LIBRARIES_W.find(lpModuleName);
        if (module != LIBRARIES_W.end()) {
            return module->second;
        }
    }

    // fallback
    return GetModuleHandleW_orig(lpModuleName);
}

static FARPROC WINAPI GetProcAddress_hook(HMODULE hModule, LPCSTR lpProcName) {

    // check for ordinal
    if (reinterpret_cast<uintptr_t>(lpProcName) <= UINT16_MAX) {

        // fallback
        return GetProcAddress_orig(hModule, lpProcName);
    }

    // check hooks
    if (lpProcName) {
        auto proc = PROCS.find(lpProcName);
        if (proc != PROCS.end()) {
            return proc->second;
        }
    }

    // fallback
    return GetProcAddress_orig(hModule, lpProcName);
}

void libraryhook_enable(HMODULE module) {
    log_info("libraryhook", "LibraryHook Attach");

    if (LHOOK_ENABLED) {
        return;
    }

    // detour
    detour::trampoline_try("kernel32.dll", "LoadLibraryA", LoadLibraryA_hook, &LoadLibraryA_orig);
    detour::trampoline_try("kernel32.dll", "LoadLibraryW", LoadLibraryW_hook, &LoadLibraryW_orig);
    detour::trampoline_try("kernel32.dll", "GetModuleHandleA", GetModuleHandleA_hook, &GetModuleHandleA_orig);
    detour::trampoline_try("kernel32.dll", "GetModuleHandleW", GetModuleHandleW_hook, &GetModuleHandleW_orig);
    detour::trampoline_try("kernel32.dll", "GetProcAddress", GetProcAddress_hook, &GetProcAddress_orig);

    // set enabled
    LHOOK_ENABLED = true;
}

void libraryhook_hook_library(std::string library_name, HMODULE library_address) {

    // add library to list
    LIBRARIES_W.insert_or_assign(s2ws(library_name), library_address);
    LIBRARIES_A.insert_or_assign(std::move(library_name), library_address);
}

void libraryhook_hook_proc(std::string proc_name, FARPROC proc_address) {

    // add proc to list
    PROCS.insert_or_assign(std::move(proc_name), proc_address);
}
