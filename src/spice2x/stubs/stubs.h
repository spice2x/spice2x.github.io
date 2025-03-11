#pragma once

#include <cstdint>

namespace stubs {

    struct ikey_status {
        uint32_t v1;
        uint8_t v2;
        uint8_t v3;
        uint8_t v4;
        uint8_t v5;
        uint32_t v6;
        uint32_t v7;
    };

    void attach();
}
