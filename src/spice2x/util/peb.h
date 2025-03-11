#pragma once

#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>

namespace peb {

    const LDR_DATA_TABLE_ENTRY* entry_first();
    const LDR_DATA_TABLE_ENTRY* entry_next(const LDR_DATA_TABLE_ENTRY* entry);
    std::string entry_name(const LDR_DATA_TABLE_ENTRY* entry);
    const PEB* peb_get();
    void peb_print();
    void obtain_modules(std::vector<std::pair<std::string, HMODULE>> *modules);
}
