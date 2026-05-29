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

    void show_available_memory() {
        constexpr double MB = 1024.0 * 1024.0;

        auto mem_status = get_mem_status();

        const auto pct = [](DWORDLONG used, DWORDLONG total) -> double {
            return total ? (100.0 * static_cast<double>(used) / static_cast<double>(total)) : 0.0;
        };

        log_info("memutils",
            "system physical memory: {:.1f} / {:.1f} MiB used ({:.1f}%)",
            mem_total_used() / MB, mem_total() / MB,
            pct(mem_total_used(), mem_total()));

        log_info("memutils",
            "system page file: {:.1f} / {:.1f} MiB used ({:.1f}%, {:.1f} MiB commit headroom)",
            vmem_total_used() / MB, vmem_total() / MB,
            pct(vmem_total_used(), vmem_total()),
            mem_status.ullAvailPageFile / MB);

        // process virtual address space - 32-bit processes die at ~2 GiB
        // (or ~4 GiB with /LARGEADDRESSAWARE) regardless of how much RAM is free
        const DWORDLONG va_used = mem_status.ullTotalVirtual - mem_status.ullAvailVirtual;
        log_info("memutils",
            "process virtual address space: {:.1f} / {:.1f} MiB used ({:.1f}%)",
            va_used / MB, mem_status.ullTotalVirtual / MB,
            pct(va_used, mem_status.ullTotalVirtual));

        // largest contiguous free block in the process address space
        // (large allocations can fail due to fragmentation even with plenty available)
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T largest_free = 0;
        uintptr_t addr = 0;
        const uintptr_t max_addr = static_cast<uintptr_t>(mem_status.ullTotalVirtual);
        while (addr < max_addr &&
               VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
            if (mbi.State == MEM_FREE && mbi.RegionSize > largest_free) {
                largest_free = mbi.RegionSize;
            }
            const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= addr) {
                break;
            }
            addr = next;
        }
        log_info("memutils",
            "process largest free virtual block: {:.1f} MiB ({:.1f}% of total VA)",
            largest_free / MB,
            pct(largest_free, mem_status.ullTotalVirtual));

        log_info("memutils",
            "process working set: {:.1f} MiB, private bytes: {:.1f} MiB",
            mem_used() / MB, vmem_used() / MB);
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
