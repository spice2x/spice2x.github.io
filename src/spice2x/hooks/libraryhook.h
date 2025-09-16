#pragma once

#include <string>
#include <functional>
#include <windows.h>

void libraryhook_enable(HMODULE module = nullptr);
void libraryhook_hook_library(std::string library_name, HMODULE library_address);
void libraryhook_hook_proc(std::string proc_name, FARPROC proc_address);
void libraryhook_load_callback(const std::function<void(const std::string&)> cb);

template<typename T>
inline void libraryhook_hook_proc(std::string proc_name, T proc_address) {
    libraryhook_hook_proc(std::move(proc_name), reinterpret_cast<FARPROC>(proc_address));
}
