#include <cstdint>

#include <windows.h>
#include <intrin.h>

#include "peb.h"
#include "utils.h"

static bool skip_entry(const LDR_DATA_TABLE_ENTRY* entry) {

    // dont skip null
    if (entry == nullptr)
        return false;

    // skip entries with invalid modules
    auto module = entry->DllBase;
    if (module == nullptr)
        return true;

    // skip stubs
    if (string_ends_with(entry->FullDllName.Buffer, L"kbt.dll"))
        return true;
    if (string_ends_with(entry->FullDllName.Buffer, L"kld.dll"))
        return true;

    // skip our own module
    return module == GetModuleHandle(NULL);
}

const LDR_DATA_TABLE_ENTRY* peb::entry_first() {

    // return first entry
    auto list_entry = (LDR_DATA_TABLE_ENTRY*) peb_get()->Ldr->InMemoryOrderModuleList.Flink;
    auto offset = offsetof(LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks) * 2;
    auto first_entry = (const LDR_DATA_TABLE_ENTRY*) ((uint8_t*) &list_entry->InMemoryOrderLinks - offset);

    // skip
    if (skip_entry(first_entry))
        return entry_next(first_entry);

    // return first entry
    return first_entry;
}

const LDR_DATA_TABLE_ENTRY* peb::entry_next(const LDR_DATA_TABLE_ENTRY* entry) {

    // check pointer for faulty loop
    if (entry == nullptr) {
        log_fatal("peb", "entry_next called with nullptr");
        return nullptr;
    }

    // finish condition
    if (entry->InMemoryOrderLinks.Flink == peb_get()->Ldr->InMemoryOrderModuleList.Flink)
        return nullptr;

    // get next entry
    auto list_entry = (LDR_DATA_TABLE_ENTRY*) entry->InMemoryOrderLinks.Flink;
    auto offset = offsetof(LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks) * 2;
    auto next_entry = (const LDR_DATA_TABLE_ENTRY*) ((uint8_t*) &list_entry->InMemoryOrderLinks - offset);

    // skip entries without module pointer
    if (skip_entry(next_entry))
        return entry_next(next_entry);

    // return next entry
    return next_entry;
}

std::string peb::entry_name(const LDR_DATA_TABLE_ENTRY* entry) {
    for (int i = entry->FullDllName.Length / 2 - 1; i > 0; i--) {
        if (entry->FullDllName.Buffer[i] == L'\\') {
            return ws2s(std::wstring(&entry->FullDllName.Buffer[i + 1]));
        }
    }
    return "unknown";
}

const PEB* peb::peb_get() {
#ifdef SPICE64
    return reinterpret_cast<const PEB *>(__readgsqword(0x60));
#else
    return reinterpret_cast<const PEB *>(__readfsdword(0x30));
#endif
}

/*
 * Prints all loaded DLLs.
 */
void peb::peb_print() {
    log_info("peb", "Detected DLLs:");
    int count = 0;
    auto cur_entry = entry_first();
    while (cur_entry != nullptr) {
        log_info("peb", "{} - {}", count++, entry_name(cur_entry));
        cur_entry = entry_next(cur_entry);
    }
}

void peb::obtain_modules(std::vector<std::pair<std::string, HMODULE>> *modules) {
    auto cur_entry = entry_first();
    while (cur_entry != nullptr) {
        modules->emplace_back(std::pair(entry_name(cur_entry), (HMODULE) cur_entry->DllBase));
        cur_entry = entry_next(cur_entry);
    }
}
