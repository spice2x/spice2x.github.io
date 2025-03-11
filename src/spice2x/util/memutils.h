#pragma once

#include <type_traits>

#include <windows.h>

namespace memutils {

    DWORDLONG mem_total();
    DWORDLONG mem_total_used();
    DWORDLONG mem_used();

    DWORDLONG vmem_total();
    DWORDLONG vmem_total_used();
    DWORDLONG vmem_used();

    /*
     * Helper class to unprotect/reprotect memory safely.
     * It will free it's mode override on destruction.
     */
    class VProtectGuard {
    public:
        explicit VProtectGuard(void *addr, size_t size, DWORD mode = PAGE_EXECUTE_READWRITE, bool reset = true);

        template<typename T>
        VProtectGuard(T &addr) : VProtectGuard(
                reinterpret_cast<void *>(addr),
                sizeof(typename std::remove_pointer<T>::type)) {}

        ~VProtectGuard();

        void dispose();

        inline bool is_bad_address() {
            return this->bad_address;
        }
    protected:
        void *addr = nullptr;
        bool bad_address = false;
        bool reset = true;
        size_t size = 0;
        DWORD old_protect = 0;
    };
}
