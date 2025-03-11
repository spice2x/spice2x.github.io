#include "memutils.h"
#include <psapi.h>
#include "util/logging.h"


namespace memutils {

    inline static MEMORYSTATUSEX get_mem_status() {
        MEMORYSTATUSEX mem_status{};
        mem_status.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&mem_status);
        return mem_status;
    }

    inline static PROCESS_MEMORY_COUNTERS_EX get_mem_counters() {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        GetProcessMemoryInfo(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS) &pmc, sizeof(pmc));
        return pmc;
    }

    DWORDLONG mem_total() {
        return get_mem_status().ullTotalPhys;
    }

    DWORDLONG mem_total_used() {
        auto status = get_mem_status();
        return status.ullTotalPhys - status.ullAvailPhys;
    }

    DWORDLONG mem_used() {
        return get_mem_counters().WorkingSetSize;
    }

    DWORDLONG vmem_total() {
        return get_mem_status().ullTotalPageFile;
    }

    DWORDLONG vmem_total_used() {
        auto status = get_mem_status();
        return status.ullTotalPageFile - status.ullAvailPageFile;
    }

    DWORDLONG vmem_used() {
        return get_mem_counters().PrivateUsage;
    }

    VProtectGuard::VProtectGuard(void *addr, size_t size, DWORD mode, bool reset)
        : addr(addr), reset(reset), size(size)
    {
        if (!VirtualProtect(this->addr, this->size, mode, &this->old_protect)) {
            auto error = GetLastError();
            log_warning("memutils", "VirtualProtect failed: {}", error);

            if (error == ERROR_INVALID_ADDRESS) {
                this->bad_address = true;
            }
        }
    }

    VProtectGuard::~VProtectGuard() {
        this->dispose();
    }

    void VProtectGuard::dispose() {
        if (this->reset && this->addr != nullptr) {
            DWORD tmp;
            if (!VirtualProtect(this->addr, this->size, this->old_protect, &tmp)) {
                log_warning("memutils", "VirtualProtect failed: {}", GetLastError());
            }
            this->addr = nullptr;
        }
    }
}
