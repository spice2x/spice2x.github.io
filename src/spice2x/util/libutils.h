#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>

#include <windows.h>

namespace libutils {

    // loaded module handle helpers
    std::filesystem::path module_file_name(HMODULE module);

    // load library helpers
    HMODULE load_library(const char *module_name, bool fatal = true);
    HMODULE load_library(const std::filesystem::path &path, bool fatal = true);
    HMODULE try_library(const char *module_name);
    HMODULE try_library(const std::filesystem::path &path);

    inline HMODULE load_library(const std::string &module_name) {
        return load_library(module_name.c_str());
    }
    inline HMODULE try_library(const std::string &module_name) {
        return try_library(module_name.c_str());
    }

    // get module handle helpers
    HMODULE get_module(const char *module_name);
    HMODULE try_module(const char *module_name);
    HMODULE try_module(const std::filesystem::path &module_path);

    inline HMODULE get_module(const std::string &module_name) {
        return get_module(module_name.c_str());
    }
    inline HMODULE try_module(const std::string &module_name) {
        return try_module(module_name.c_str());
    }

    // get proc address helpers
    FARPROC get_proc(const char *proc_name);
    FARPROC get_proc(HMODULE module, const char *proc_name);
    FARPROC get_proc_list(HMODULE module, std::initializer_list<const char *> list);
    FARPROC try_proc(const char *proc_name);
    FARPROC try_proc(HMODULE module, const char *proc_name);
    FARPROC try_proc_list(HMODULE module, std::initializer_list<const char *> list);

    template<typename T>
    inline T get_proc(const char *proc_name) {
        return reinterpret_cast<T>(get_proc(proc_name));
    }
    template<typename T>
    inline T get_proc(HMODULE module, const char *proc_name) {
        return reinterpret_cast<T>(get_proc(module, proc_name));
    }
    template<typename T>
    inline T get_proc_list(HMODULE module, std::initializer_list<const char *> list) {
        return reinterpret_cast<T>(get_proc_list(module, list));
    }
    template<typename T>
    inline T try_proc(const char *proc_name) {
        return reinterpret_cast<T>(try_proc(proc_name));
    }
    template<typename T>
    inline T try_proc(HMODULE module, const char *proc_name) {
        return reinterpret_cast<T>(try_proc(module, proc_name));
    }
    template<typename T>
    inline T try_proc_list(HMODULE module, std::initializer_list<const char *> list) {
        return reinterpret_cast<T>(try_proc_list(module, list));
    }

    // offset helpers
    intptr_t rva2offset(IMAGE_NT_HEADERS *nt_headers, intptr_t rva);
    intptr_t rva2offset(const std::filesystem::path &path, intptr_t rva);
    intptr_t offset2rva(IMAGE_NT_HEADERS *nt_headers, intptr_t offset);
    intptr_t offset2rva(const std::filesystem::path &path, intptr_t offset);
}
