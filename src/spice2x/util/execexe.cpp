#include "execexe.h"

#include "util/logging.h"
#include "util/libutils.h"
#include "util/utils.h"
#include "util/detour.h"

namespace execexe {

    static HMODULE execexe_module = nullptr;
    static decltype(&LoadLibraryW) execexe_LoadLibraryW = nullptr;
    static decltype(&GetModuleHandleW) execexe_GetModuleHandleW = nullptr;
    static decltype(&GetProcAddress) execexe_GetProcAddress = nullptr;
    static decltype(&CreateFileA) execexe_CreateFileA = nullptr;
    static decltype(&CreateFileW) execexe_CreateFileW = nullptr;
    static decltype(&CloseHandle) execexe_CloseHandle = nullptr;

    static std::wstring plugins_dir;
    static acioemu::ACIOHandle *acio = nullptr;
    static std::wstring port_name;
    static bool port_opened = false;

    static HANDLE WINAPI execexe_CreateFileA_hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                  LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                                  DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
        const auto lpFileNameW = s2ws(lpFileName);
        if (lpFileNameW == port_name) {
            if (!port_opened) {
                port_opened = acio->open(port_name.c_str());
            } else {
                log_info("execexe", "ignored handle open. ({})", ws2s(port_name));
            }
            SetLastError(0);
            return (HANDLE) acio;
        }
        return execexe_CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                   dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    static HANDLE WINAPI execexe_CreateFileW_hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                  LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                                  DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
        if (lpFileName == port_name && acio->open(lpFileName)) {
            if (!port_opened) {
                port_opened = acio->open(port_name.c_str());
            } else {
                log_info("execexe", "ignored handle open. ({})", ws2s(port_name));
            }
            SetLastError(0);
            return (HANDLE) acio;
        } else {
            return execexe_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        }
    }

    static WINBOOL WINAPI execexe_CloseHandle_hook(HANDLE hObject) {
        if (hObject == acio && port_opened) {
            log_info("execexe", "ignored handle close. ({})", ws2s(port_name));
            return TRUE;
        }
        return execexe_CloseHandle(hObject);
    }

    HMODULE init() {
        execexe_module = libutils::load_library("execexe.dll");
        execexe_LoadLibraryW = libutils::get_proc<decltype(&LoadLibraryW)>(execexe_module, MAKEINTRESOURCE(34));
        execexe_GetModuleHandleW = libutils::get_proc<decltype(&GetModuleHandleW)>(execexe_module, MAKEINTRESOURCE(25));
        execexe_GetProcAddress = libutils::get_proc<decltype(&GetProcAddress)>(execexe_module, MAKEINTRESOURCE(27));
        execexe_CloseHandle = libutils::get_proc<decltype(&CloseHandle)>(execexe_module, MAKEINTRESOURCE(7));
        execexe_CreateFileA = libutils::get_proc<decltype(&CreateFileA)>(execexe_module, MAKEINTRESOURCE(9));
        execexe_CreateFileW = libutils::get_proc<decltype(&CreateFileW)>(execexe_module, MAKEINTRESOURCE(11));

        auto module_path = libutils::module_file_name(nullptr);
        module_path = module_path.replace_extension("");
        module_path = module_path.replace_filename(module_path.filename().wstring() + L"_Data");
        plugins_dir = (module_path / L"Plugins" / L"x86_64").wstring() + L"\\";

        return execexe_module;
    }

    void init_port_hook(const std::wstring &portName, acioemu::ACIOHandle *acioHandle) {
        static bool init = false;
        if (init)
            return;
        init = true;

        port_name = portName;
        acio = acioHandle;
        detour::trampoline_try("execexe.dll", MAKEINTRESOURCE(7),
                               execexe_CloseHandle_hook, &execexe_CloseHandle);
        detour::trampoline_try("execexe.dll", MAKEINTRESOURCE(9),
                               execexe_CreateFileA_hook, &execexe_CreateFileA);
        detour::trampoline_try("execexe.dll", MAKEINTRESOURCE(11),
                               execexe_CreateFileW_hook, &execexe_CreateFileW);
    }

    HMODULE load_library(const char *module_name, bool fatal) {
        std::wstring module_name_w = s2ws(module_name);
        std::wstring plugin_path = plugins_dir + module_name_w;
        HMODULE module = execexe_LoadLibraryW(plugin_path.c_str());
        if (module != nullptr) {
            return module;
        }

        module = execexe_LoadLibraryW(module_name_w.c_str());
        if (module != nullptr) {
            return module;
        }

        if (fatal) {
            log_fatal("execexe", "failed to load library {}", module_name);
        }

        return nullptr;
    }

    HMODULE get_module(const char *module_name, bool fatal) {
        std::wstring module_name_w = s2ws(module_name);
        std::wstring plugin_path = plugins_dir + module_name_w;

        HMODULE module = execexe_GetModuleHandleW(plugin_path.c_str());
        if (module != nullptr) {
            return module;
        }

        module = execexe_GetModuleHandleW(module_name_w.c_str());
        if (module != nullptr) {
            return module;
        }

        if (fatal) {
            log_fatal("execexe", "failed to get module {}", module_name);
        }

        return nullptr;
    }

    FARPROC get_proc(HMODULE module, const char *proc_name, bool fatal) {
        FARPROC proc = execexe_GetProcAddress(module, proc_name);
        if (proc != nullptr) {
            return proc;
        }

        if (fatal) {
            log_fatal("execexe", "proc {} not found", proc_name);
        }

        return nullptr;
    }

    bool trampoline(const char *dll, const char *func, void *hook, void **orig) {
        HMODULE module = get_module(dll);
        FARPROC proc = get_proc(module, func);

        return detour::trampoline(
                reinterpret_cast<void *>(proc),
                hook,
                orig
        );
    }

    bool trampoline_try(const char *dll, const char *func, void *hook, void **orig) {
        HMODULE module = get_module(dll, false);
        if (module == nullptr) {
            return false;
        }

        FARPROC proc = get_proc(module, func, false);
        if (proc == nullptr) {
            return false;
        }

        return detour::trampoline(
                reinterpret_cast<void *>(proc),
                hook,
                orig
        );
    }
}
