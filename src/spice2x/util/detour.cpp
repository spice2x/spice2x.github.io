#include "detour.h"

#include <mutex>

#include "external/minhook/include/MinHook.h"

#include "logging.h"
#include "memutils.h"
#include "peb.h"
#include "utils.h"

static void minhook_init() {
    static std::once_flag init;

    std::call_once(init, []() {
        MH_Initialize();
    });
}

bool detour::inline_hook(void *new_adr, void *address) {
#ifdef SPICE64
    if (address) {
        unsigned char patch[] = {0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0};
        *(unsigned long long*) &patch[2] = (unsigned long long) new_adr;
        unsigned int OldProtect = 0;
        unsigned int Temp = 0;
        VirtualProtect(address, 4096, PAGE_EXECUTE_READWRITE, (PDWORD) &OldProtect);
        memcpy(address, patch, sizeof(patch));
        VirtualProtect(address, 4096, OldProtect, (PDWORD) &Temp);
        return true;
    }
    return false;
#else
    if (address) {
        unsigned int OldProtect = 0;
        unsigned int Temp = 0;
        int call = (int) ((signed long long) ((uint8_t*) new_adr - (long long) address - 5));
        VirtualProtect(address, 4096, PAGE_EXECUTE_READWRITE, (PDWORD) &OldProtect);
        *((unsigned char *) (address)) = 0xE9;
        *((int *) ((uint8_t*) address + 1)) = call;
        VirtualProtect(address, 4096, OldProtect, (PDWORD) &Temp);
        return true;
    }
    return false;
#endif
}

bool detour::inline_noprotect(void *new_adr, void *address) {
#ifdef SPICE64
    if (address) {
        unsigned char patch[] = {0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0};
        *(unsigned long long *) &patch[2] = (unsigned long long) new_adr;
        memcpy(address, patch, sizeof(patch));
        return true;
    }
    return false;
#else
    if (address) {
        *((unsigned char *) (address)) = 0xE9;
        *((int *) ((uint8_t*) address + 1)) = (int) ((signed long long) ((uint8_t*) new_adr - (long long) address - 5));
        return true;
    }
    return false;
#endif
}

bool detour::inline_preserve(void *new_adr, void *address, char *data) {
#ifdef SPICE64
    if (address) {
        unsigned char patch[] = {0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0};
        *(unsigned long long*) &patch[2] = (unsigned long long) new_adr;
        unsigned int OldProtect = 0;
        VirtualProtect(address, 4096, PAGE_EXECUTE_READWRITE, (PDWORD) &OldProtect);
        memcpy(data, address, sizeof(patch));
        memcpy(address, patch, sizeof(patch));
        return true;
    }
    return false;
#else
    if (address) {
        unsigned int OldProtect = 0;
        int call = (int) ((signed long long) ((uint8_t*) new_adr - (long long) address - 5));
        VirtualProtect(address, 4096, PAGE_EXECUTE_READWRITE, (PDWORD) &OldProtect);
        memcpy(data, address, 5);
        *((unsigned char *) (address)) = 0xE9;
        *((int *) ((uint8_t*) address + 1)) = call;
        return true;
    }
    return false;
#endif
}

bool detour::inline_restore(void *address, char *data) {
#ifdef SPICE64
    if (address) {
        memcpy(address, data, 12);
        return true;
    }
    return false;
#else
    if (address) {
        memcpy(address, data, 5);
        return true;
    }
    return false;
#endif
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

static void *pe_offset(void *ptr, size_t offset) {
    if (offset == 0) {
        return nullptr;
    }

    return reinterpret_cast<uint8_t *>(ptr) + offset;
}

void **detour::iat_find(const char *function, HMODULE module, const char *iid_name) {

    // check module
    if (module == nullptr) {
        return nullptr;
    }

    // check signature
    const IMAGE_DOS_HEADER *pImgDosHeaders = (IMAGE_DOS_HEADER *) module;
    if (pImgDosHeaders->e_magic != IMAGE_DOS_SIGNATURE) {
        log_fatal("detour", "signature mismatch ({} != {})", pImgDosHeaders->e_magic, IMAGE_DOS_SIGNATURE);
    }

    // get import table
    const auto nt_headers = reinterpret_cast<IMAGE_NT_HEADERS *>(pe_offset(module, pImgDosHeaders->e_lfanew));
    const auto data_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto import_table = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(pe_offset(module, data_dir->VirtualAddress));

    // iterate import descriptors
    DWORD iid_count = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *iid = import_table; iid_count < data_dir->Size && iid->Name != 0; iid++) {
        iid_count++;

        // check name
        if (iid_name != nullptr) {

            // get name
            auto name = reinterpret_cast<PCSTR>(RtlOffsetToPointer(module, iid->Name));

            // skip if it's not the correct module
            if (_stricmp(name, iid_name) != 0) {
                continue;
            }
        }

        // iterate functions
        for (SIZE_T funcIdx = 0; *(funcIdx + (LPVOID *) pe_offset(module, iid->FirstThunk)) != nullptr; funcIdx++) {

            // get function name
            auto imports = reinterpret_cast<uintptr_t *>(pe_offset(module, iid->OriginalFirstThunk));
            if (imports == nullptr) {
                break;
            }

            auto import = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(pe_offset(module, imports[funcIdx]));

            auto import_name = reinterpret_cast<volatile void *>(import->Name);
            auto import_name_ptr = reinterpret_cast<uintptr_t>(import->Name);

            // check string
            if (import_name != nullptr && !IMAGE_SNAP_BY_ORDINAL(import_name_ptr)) {

                // compare function names
                if (!_stricmp(function, import->Name)) {
                    return funcIdx + (LPVOID *) pe_offset(module, iid->FirstThunk);
                }
            }
        }
    }

    // nothing found
    return nullptr;
}

void **detour::iat_find_ordinal(const char *iid_name, DWORD ordinal, HMODULE module) {

    // check module
    if (module == nullptr) {
        return nullptr;
    }

    // check signature
    const auto pImgDosHeaders = reinterpret_cast<IMAGE_DOS_HEADER *>(module);
    if (pImgDosHeaders->e_magic != IMAGE_DOS_SIGNATURE) {
        log_fatal("detour", "signature error");
    }

    // get import table
    const auto nt_headers = reinterpret_cast<IMAGE_NT_HEADERS *>(pe_offset(module, pImgDosHeaders->e_lfanew));
    const auto data_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto import_table = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(pe_offset(module, data_dir->VirtualAddress));

    // iterate import descriptors
    DWORD iid_count = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *iid = import_table; iid_count < data_dir->Size && iid->Name != 0; iid++) {
        iid_count++;

        // get name, original first thunk (ILT), and array of function pointers
        auto name = reinterpret_cast<PCSTR>(pe_offset(module, iid->Name));
        auto OriginalFirstThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(pe_offset(module, iid->OriginalFirstThunk));
        auto FirstThunk = reinterpret_cast<void **>(pe_offset(module, iid->FirstThunk));

        // skip if it's not the correct module
        if (_stricmp(name, iid_name) != 0) {
            continue;
        }

        // iterate functions
        for (SIZE_T funcIdx = 0; *(funcIdx + (LPVOID *) pe_offset(module, iid->FirstThunk)) != nullptr; funcIdx++) {
            auto thunk = &OriginalFirstThunk[funcIdx];
            if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {

                // check if the ordinal matches
                if (IMAGE_ORDINAL(thunk->u1.Ordinal) == ordinal) {
                    return &FirstThunk[funcIdx];
                }
            }
        }
    }

    // nothing found
    return nullptr;
}

void **detour::iat_find_proc(const char *iid_name, void *proc, HMODULE module) {

    // check module
    if (module == nullptr) {
        return nullptr;
    }

    // check proc
    if (proc == nullptr) {
        return nullptr;
    }

    // check signature
    const auto pImgDosHeaders = reinterpret_cast<IMAGE_DOS_HEADER *>(module);
    if (pImgDosHeaders->e_magic != IMAGE_DOS_SIGNATURE) {
        log_fatal("detour", "signature error");
    }

    // get import table
    const auto nt_headers = reinterpret_cast<IMAGE_NT_HEADERS *>(pe_offset(module, pImgDosHeaders->e_lfanew));
    const auto data_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto import_table = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(pe_offset(module, data_dir->VirtualAddress));

    // iterate import descriptors
    DWORD iid_count = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *iid = import_table; iid_count < data_dir->Size && iid->Name != 0; iid++) {
        iid_count++;

        // get name and array of function pointers
        auto name = reinterpret_cast<PCSTR>(pe_offset(module, iid->Name));
        auto FirstThunk = reinterpret_cast<void **>(pe_offset(module, iid->FirstThunk));

        // skip if it's not the correct module
        if (_stricmp(name, iid_name) != 0) {
            continue;
        }

        // iterate functions
        for (SIZE_T funcIdx = 0; *(funcIdx + (LPVOID *) pe_offset(module, iid->FirstThunk)) != nullptr; funcIdx++) {

            // check if the destination matches proc
            if (FirstThunk[funcIdx] == proc) {
                return &FirstThunk[funcIdx];
            }
        }
    }

    // nothing found
    return nullptr;
}

#pragma clang diagnostic pop

void *detour::iat_try(const char *function, void *new_func, HMODULE module, const char *iid_name) {

    // apply to all loaded modules by default
    if (module == nullptr) {
        void *ret = nullptr;
        auto cur_entry = peb::entry_first();

        while (cur_entry != nullptr) {
            module = reinterpret_cast<HMODULE>(cur_entry->DllBase);

            if (module) {
                auto old_func = iat_try(function, new_func, module, iid_name);
                ret = ret != nullptr ? ret : old_func;
            }

            cur_entry = peb::entry_next(cur_entry);
        }

        return ret;
    }

    // find entry
    void **func_ptr = iat_find(function, module, iid_name);

    // check entry
    if (!func_ptr || !*func_ptr) {
        return nullptr;
    }

    // save original
    void *real_func = *func_ptr;

    // patch
    memutils::VProtectGuard func_ptr_guard(func_ptr, sizeof(LPVOID));
    *func_ptr = new_func;

    return real_func;
}

void *detour::iat_try_ordinal(const char *iid_name, DWORD ordinal, void *new_func, HMODULE module) {

    // fail when no module was specified
    if (module == nullptr) {
        return nullptr;
    }

    // find entry
    void **func_ptr = iat_find_ordinal(iid_name, ordinal, module);

    // check entry
    if (!func_ptr || !*func_ptr) {
        return nullptr;
    }

    // save original
    void *real_func = *func_ptr;

    // patch
    memutils::VProtectGuard func_ptr_guard(func_ptr, sizeof(LPVOID));
    *func_ptr = new_func;

    return real_func;
}

void *detour::iat_try_proc(const char *iid_name, void *proc, void *new_func, HMODULE module) {

    // check proc
    if (proc == nullptr) {
        return nullptr;
    }

    // apply to all loaded modules by default
    if (module == nullptr) {
        void *ret = nullptr;
        auto cur_entry = peb::entry_first();

        while (cur_entry != nullptr) {
            module = reinterpret_cast<HMODULE>(cur_entry->DllBase);

            if (module) {
                auto old_func = iat_try_proc(iid_name, proc, new_func, module);
                ret = ret != nullptr ? ret : old_func;
            }

            cur_entry = peb::entry_next(cur_entry);
        }

        return ret;
    }

    // find entry
    void **func_ptr = iat_find_proc(iid_name, proc, module);

    // check entry
    if (!func_ptr || !*func_ptr) {
        return nullptr;
    }

    // save original
    void *real_func = *func_ptr;

    // patch
    memutils::VProtectGuard func_ptr_guard(func_ptr, sizeof(LPVOID));
    *func_ptr = new_func;

    return real_func;
}

void *detour::iat(const char *function, void *new_func, HMODULE module) {
    void *func_ptr = iat_try(function, new_func, module);
    if (!func_ptr) {
        log_fatal("detour", "could not hook {}", function);
    }
    return func_ptr;
}

void *detour::iat_ordinal(const char *iid_name, DWORD ordinal, void *new_func, HMODULE module) {
    void *func_ptr = iat_try_ordinal(iid_name, ordinal, new_func, module);
    if (!func_ptr) {
        log_fatal("detour", "could not hook {}: {}", iid_name, ordinal);
    }
    return func_ptr;
}

bool detour::trampoline(const char *dll, const char *func, void *hook, void **orig) {
    if (!trampoline_try(dll, func, hook, orig)) {
        log_fatal("detour", "could not insert trampoline for {}:{}", dll, func);
        return false;
    }
    return true;
}

bool detour::trampoline(void *func, void *hook, void **orig) {
    if (!trampoline_try(func, hook, orig)) {
        log_fatal("detour", "could not insert trampoline for {}", func);
        return false;
    }
    return true;
}

bool detour::trampoline_try(const char *dll, const char *func, void *hook, void **orig) {
    minhook_init();
    auto dll_w = s2ws(dll);
    auto target = *orig;
    auto create = MH_CreateHookApi(dll_w.c_str(), func, hook, orig);
    if (create != MH_OK) {
        // log_warning("detour", "MH_CreateHookApi({}, {}): {}", dll, func, MH_StatusToString(create));
        return false;
    }
    return !(MH_EnableHook(target) != MH_OK);
}

bool detour::trampoline_try(void *func, void *hook, void **orig) {
    minhook_init();
    auto target = *orig;
    if (MH_CreateHook(func, hook, orig) != MH_OK) {
        return false;
    }
    return !(MH_EnableHook(target) != MH_OK);
}
