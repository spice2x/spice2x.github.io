#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace crypt {
    extern bool INITIALIZED;

    void init();
    void dispose();
    void random_bytes(void *data, size_t length);
    std::string base64_encode(const uint8_t *ptr, size_t length);
}
