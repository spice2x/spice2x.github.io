#pragma once

#include <initializer_list>
#include <string>

#include <windows.h>

#define VTBL_TYPE(TYPE, MEMBER) decltype(((TYPE *) 0)->lpVtbl->MEMBER)

namespace detour {

    /*
     * Inline hooks
     */

    bool inline_hook(void *new_adr, void *address);
    bool inline_noprotect(void *new_adr, void *address);
    bool inline_preserve(void *new_adr, void *address, char *data);
    bool inline_restore(void *address, char *data);
    bool trampoline(const char *dll, const char *func, void *hook, void **orig);
    bool trampoline(void *func, void *hook, void **orig);
    bool trampoline_try(const char *dll, const char *func, void *hook, void **orig);
    bool trampoline_try(void *func, void *hook, void **orig);

    /*
     * Inline hook aliases
     */

    template<typename T, typename U>
    inline bool inline_hook(T new_adr, U address) {
        return inline_hook(
                reinterpret_cast<void *>(new_adr),
                reinterpret_cast<void *>(address));
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
    inline bool trampoline(T func, T hook, T *orig) {
        return trampoline(
                reinterpret_cast<void *>(func),
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
    template<typename T>
    inline bool trampoline_try(T func, T hook, T *orig) {
        return trampoline_try(
                reinterpret_cast<void *>(func),
                reinterpret_cast<void *>(hook),
                reinterpret_cast<void **>(orig));
    }

    /*
     * IAT hooks
     */

    // for finding IAT entries - you probably won't need to use those yourself
    void **iat_find(const char *function, HMODULE module, const char *iid_name = nullptr);
    void **iat_find_ordinal(const char *iid_name, DWORD ordinal, HMODULE module);
    void **iat_find_proc(const char *iid_name, void *proc, HMODULE module);

    // best effort hooks - they will fail silently
    void *iat_try(const char *function, void *new_func, HMODULE module = nullptr, const char *iid_name = nullptr);
    void *iat_try_ordinal(const char *iid_name, DWORD ordinal, void *new_func, HMODULE module);
    void *iat_try_proc(const char *iid_name, void *proc, void *new_func, HMODULE module = nullptr);

    template<typename T>
    inline T iat_try(const char *function, T new_func, HMODULE module = nullptr, const char *iid_name = nullptr) {
        return reinterpret_cast<T>(iat_try(function, reinterpret_cast<void *>(new_func), module, iid_name));
    }
    template<typename T>
    inline T iat_try_ordinal(const char *iid_name, DWORD ordinal, T new_func, HMODULE module) {
        return reinterpret_cast<T>(iat_try_ordinal(iid_name, ordinal, reinterpret_cast<void *>(new_func), module));
    }
    template<typename T>
    inline T iat_try_proc(const char *iid_name, T proc, T new_func, HMODULE module = nullptr) {
        return reinterpret_cast<T>(iat_try_proc(
                    iid_name,
                    reinterpret_cast<void *>(proc),
                    reinterpret_cast<void *>(new_func),
                    module));
    }

    // guaranteed hooks - they will stop the program on failure
    void *iat(const char *function, void *new_func, HMODULE module = nullptr);
    void *iat_ordinal(const char *iid_name, DWORD ordinal, void *new_func, HMODULE module);

    template<typename T>
    inline T iat(const char *iid_name, T new_func, HMODULE module = nullptr) {
        return reinterpret_cast<T>(iat(iid_name, reinterpret_cast<void *>(new_func), module));
    }
    template<typename T>
    inline T iat_ordinal(const char *iid_name, DWORD ordinal, T new_func, HMODULE module = nullptr) {
        return reinterpret_cast<T>(iat_ordinal(iid_name, ordinal, reinterpret_cast<void *>(new_func), module));
    }
}
