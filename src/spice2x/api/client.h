#pragma once

#include <atomic>
#include <cstdint>

namespace api {

    extern std::atomic_uint32_t CLIENT_COUNT;

    inline bool has_clients() {
        return CLIENT_COUNT.load(std::memory_order_relaxed) > 0;
    }
}
