#include "libutils.h"

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>

#include "logging.h"
#include "utils.h"
#include "peb.h"
#include "util/fileutils.h"

std::filesystem::path libutils::module_file_name(HMODULE module) {
    std::wstring buf;
    buf.resize(MAX_PATH + 1);

    while (true) {
        auto size = GetModuleFileNameW(nullptr, buf.data(), buf.capacity());

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            buf.resize(size);
            break;
        }

        buf.resize(buf.size() * 2);
    }

    return std::filesystem::path(std::move(buf));
}

static inline void load_library_fail(const std::string &file_name, bool fatal) {
    std::string info_str { fmt::format(
        "DLL failed to load - this is a common error. Please carefully read ALL of the following steps for a fix:\n"
        " 1. Confirm if the file ({}) exists on the disk and check the file permissions.\n"
        " 2. Follow this link and install DLL prerequisites on this list:\n"
        "        https://github.com/spice2x/spice2x.github.io/wiki/DLL-Dependencies \n"
        " 3. Still have problems after installing from above and rebooting PC?\n"
        "     a. Avoid manually specifying DLL path (-exec) and module directory (-modules); let spice2x auto-detect unless you have a good reason not to\n"
        "     b. Ensure you do NOT have multiple copies of the game DLLs (e.g., in contents and in contents\\modules)\n"
        "     c. Certain games require specific NVIDIA DLLs when running with AMD/Intel GPUs (hint: look inside stub directory for DLLs)\n"
        "* 4. (For advanced users) if none of the above helps, find the missing dependency using:\n"
        "     a. https://github.com/lucasg/Dependencies (recommended for most) \n"
        "     b. http://www.dependencywalker.com/ (for old OS) \n"
    , file_name) };
    if (fatal) {
        log_fatal("libutils", "{}", info_str);
    } else {
        log_warning("libutils", "{}", info_str);
    }
}

HMODULE libutils::load_library(const char *module_name, bool fatal) {
    HMODULE module = LoadLibraryA(module_name);

    if (!module) {
        log_warning("libutils", "'{}' couldn't be loaded: {}", module_name, get_last_error_string());
        std::string file_name(PathFindFileNameA(module_name));
        load_library_fail(file_name, fatal);
    }

    return module;
}

HMODULE libutils::load_library(const std::filesystem::path &path, bool fatal) {
    HMODULE module = LoadLibraryW(path.c_str());

    if (!module) {
        log_warning("libutils", "'{}' couldn't be loaded: {}", path.string(), get_last_error_string());
        load_library_fail(path.filename().string(), fatal);
    }

    return module;
}

HMODULE libutils::try_library(const char *module_name) {
    return LoadLibraryA(module_name);
}

HMODULE libutils::try_library(const std::filesystem::path &path) {
    return LoadLibraryW(path.c_str());
}

HMODULE libutils::get_module(const char *module_name) {
    HMODULE module = GetModuleHandleA(module_name);
    if (!module) {
        log_fatal("libutils", "'{}' could not be loaded: {}", module_name, get_last_error_string());
    }
    return module;
}

HMODULE libutils::try_module(const char *module_name) {
    return GetModuleHandleA(module_name);
}

HMODULE libutils::try_module(const std::filesystem::path &module_path) {
    return GetModuleHandleW(module_path.c_str());
}

FARPROC libutils::get_proc(const char *proc_name) {

    // iterate loaded modules
    auto cur_entry = peb::entry_first();
    while (cur_entry != nullptr) {

        // check if this module contains the function
        auto proc = try_proc(reinterpret_cast<HMODULE>(cur_entry->DllBase), proc_name);
        if (proc) {
            return proc;
        }

        // next entry
        cur_entry = peb::entry_next(cur_entry);
    }

    // function not found
    log_fatal("libutils", "'{}' not found", proc_name);

    return 0;
}

FARPROC libutils::get_proc(HMODULE module, LPCSTR proc) {
    auto value = GetProcAddress(module, proc);

    if (!value) {
        log_fatal("libutils", "'{}' not found", proc);
    }

    return value;
}

FARPROC libutils::get_proc_list(HMODULE module, std::initializer_list<const char *> list) {
    FARPROC value = nullptr;
    for (auto proc_name : list) {
        value = GetProcAddress(module, proc_name);
        if (value) {
            return value;
        }
    }

    // error out
    log_fatal("libutils", "{} not found", *list.begin());
    return nullptr;
}

FARPROC libutils::try_proc(const char *proc_name) {

    // iterate loaded modules
    auto cur_entry = peb::entry_first();
    while (cur_entry != nullptr) {

        // check if this module contains the function
        auto proc = try_proc(reinterpret_cast<HMODULE>(cur_entry->DllBase), proc_name);
        if (proc) {
            return proc;
        }

        // next entry
        cur_entry = peb::entry_next(cur_entry);
    }

    // function not found
    return 0;
}

FARPROC libutils::try_proc(HMODULE module, const char *proc_name) {
    FARPROC value = GetProcAddress(module, proc_name);
    if (!value) {
        return 0;
    }
    return value;
}

FARPROC libutils::try_proc_list(HMODULE module, std::initializer_list<const char *> list) {
    for (auto proc_name : list) {
        auto value = GetProcAddress(module, proc_name);
        if (value) {
            return value;
        }
    }
    return nullptr;
}

intptr_t libutils::rva2offset(IMAGE_NT_HEADERS *nt_headers, intptr_t rva) {

    // iterate sections
    const auto section_count = nt_headers->FileHeader.NumberOfSections;
    const IMAGE_SECTION_HEADER *section_header = IMAGE_FIRST_SECTION(nt_headers);
    for (size_t i = 0; i < section_count; i++) {

        // check if RVA is within section
        if (section_header->VirtualAddress <= (DWORD) rva) {
            if ((section_header->VirtualAddress + section_header->Misc.VirtualSize) > (DWORD) rva) {
                rva -= section_header->VirtualAddress;
                rva += section_header->PointerToRawData;
                return rva;
            }
        }

        // next section
        section_header++;
    }

    // offset out of bounds
    return -1;
}


intptr_t libutils::rva2offset(const std::filesystem::path &path, intptr_t rva) {

    // open file
    HANDLE dll_file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0);
    if (!dll_file) {
        return ~0;
    }

    // create file mapping
    HANDLE dll_mapping = CreateFileMappingW(dll_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!dll_mapping) {
        CloseHandle(dll_file);
        log_warning("libutils", "could not create file mapping for {}", path.string());
        return -1;
    }

    // map view of file
    LPVOID dll_file_base = MapViewOfFile(dll_mapping, FILE_MAP_READ, 0, 0, 0);
    if (!dll_file_base) {
        CloseHandle(dll_file);
        CloseHandle(dll_mapping);
        log_warning("libutils", "could not map view of file for {}", path.string());
        return -1;
    }

    // get offset
    intptr_t offset = -1;
    auto dll_dos = reinterpret_cast<PIMAGE_DOS_HEADER>(dll_file_base);
    if (dll_dos->e_magic == IMAGE_DOS_SIGNATURE) {
        auto dll_nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uint8_t *>(dll_dos) + dll_dos->e_lfanew);
        offset = libutils::rva2offset(dll_nt, rva);
    }

    // clean up and return
    UnmapViewOfFile(dll_file_base);
    CloseHandle(dll_file);
    CloseHandle(dll_mapping);
    return offset;
}

intptr_t libutils::offset2rva(IMAGE_NT_HEADERS *nt_headers, intptr_t offset) {

    // iterate sections
    auto section_count = nt_headers->FileHeader.NumberOfSections;
    PIMAGE_SECTION_HEADER section_header = IMAGE_FIRST_SECTION(nt_headers);
    for (int i = 0; i < section_count; i++) {

        // check if offset is within section
        if (section_header->PointerToRawData <= static_cast<DWORD>(offset)) {
            if ((section_header->PointerToRawData + section_header->SizeOfRawData) > static_cast<DWORD>(offset)) {
                offset -= section_header->PointerToRawData;
                offset += section_header->VirtualAddress;
                return offset;
            }
        }

        // next section
        section_header++;
    }

    // offset out of bounds
    return -1;
}

intptr_t libutils::offset2rva(const std::filesystem::path &path, intptr_t offset) {

    // open file
    HANDLE dll_file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0);
    if (!dll_file) {
        return -1;
    }

    // create file mapping
    HANDLE dll_mapping = CreateFileMappingW(dll_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!dll_mapping) {
        log_warning("libutils", "could not create file mapping for {}: {}", path.string(), get_last_error_string());
        CloseHandle(dll_file);
        return -1;
    }

    // map view of file
    LPVOID dll_file_base = MapViewOfFile(dll_mapping, FILE_MAP_READ, 0, 0, 0);
    if (!dll_file_base) {
        log_warning("libutils", "could not map view of file for {}: {}", path.string(), get_last_error_string());
        CloseHandle(dll_file);
        CloseHandle(dll_mapping);
        return -1;
    }

    // get RVA
    intptr_t rva = -1;
    auto dll_dos = reinterpret_cast<PIMAGE_DOS_HEADER>(dll_file_base);
    if (dll_dos->e_magic == IMAGE_DOS_SIGNATURE) {
        auto dll_nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uint8_t *>(dll_dos) + dll_dos->e_lfanew);
        rva = libutils::offset2rva(dll_nt, offset);
    }

    // clean up
    UnmapViewOfFile(dll_file_base);
    CloseHandle(dll_file);
    CloseHandle(dll_mapping);

    return rva;
}

void libutils::check_duplicate_dlls() {
    const auto &spice_bin_path = libutils::module_file_name(nullptr).parent_path();
    if (MODULE_PATH == spice_bin_path) {
        return;
    }

    for (const auto &file : std::filesystem::directory_iterator(MODULE_PATH)) {
        const auto &filename = file.path().filename();
        const auto extension = strtolower(filename.extension().string());

        if (extension == ".dll" &&
            fileutils::file_exists(spice_bin_path / filename)) {
            log_warning(
                "libutils",
                "DLL CONFLICT WARNING\n\n\n"
                "-------------------------------------------------------------------\n"
                "WARNING - WARNING - WARNING - WARNING - WARNING - WARNING - WARNING\n"
                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                "{} exists in BOTH of these directories:\n\n"
                "    1. {}\n"
                "    2. {}\n\n"
                "due to Windows DLL load order rules, #1 will load instead of #2.\n"
                "this has unintended consequences and may crash your game!\n"
                "resolve the conflict by deleting the stale copy of the DLL\n"
                "-------------------------------------------------------------------\n\n\n",
                filename.string(),
                (spice_bin_path / filename).string(),
                (MODULE_PATH / filename).string());
        }
    }
}

void libutils::warn_if_dll_exists(const std::string &file_name) {
    if (fileutils::file_exists(MODULE_PATH / file_name)) {
        log_info("libutils", "found user-supplied {} in modules directory", file_name);
        return;
    }
    const auto &spice_bin_path = libutils::module_file_name(nullptr).parent_path();
    if (fileutils::file_exists(spice_bin_path / file_name)) {
        log_info("libutils", "found user-supplied {} next to spice executable path", file_name);
        return;
    }
}