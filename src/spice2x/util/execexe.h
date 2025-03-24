#pragma once

#include <string>
#include <windows.h>
#include "acioemu/handle.h"

namespace execexe {
    HMODULE init();
    void init_port_hook(const std::wstring &portName, acioemu::ACIOHandle *acioHandle);
    HMODULE load_library(const char *module_name, bool fatal = true);
    FARPROC get_proc(HMODULE module, const char *proc_name, bool fatal = true);
    HMODULE get_module(const char *module_name, bool fatal = true);
    bool trampoline(const char *dll, const char *func, void *hook, void **orig);
    bool trampoline_try(const char *dll, const char *func, void *hook, void **orig);

    template<typename T>
    inline T get_proc(HMODULE module, const char *proc_name, bool fatal = true) {
        return reinterpret_cast<T>(get_proc(module, proc_name, fatal));
    }

    template<typename T>
    inline bool trampoline(const char *dll, const char *func, T hook, T *orig) {
        return trampoline(
                dll,
                func,
                reinterpret_cast<void *>(hook),
                reinterpret_cast<void **>(orig));
    }

    template<typename T>
    inline bool trampoline_try(const char *dll, const char *func, T hook, T *orig) {
        return trampoline_try(
                dll,
                func,
                reinterpret_cast<void *>(hook),
                reinterpret_cast<void **>(orig));
    }
}
