#pragma once

#include <cstdint>
#include <cstddef>

namespace acio2emu::detail {
    inline uint8_t crc4_lgp_c(uint8_t crc, const uint8_t *data, size_t len) {
        static constexpr uint8_t tbl[] = { 
            0x00, 0x0D, 0x03, 0x0E, 
            0x06, 0x0B, 0x05, 0x08,
            0x0C, 0x01, 0x0F, 0x02,
            0x0A, 0x07, 0x09, 0x04,
        };

        crc &= 15;
        for (size_t i = 0; i < len; i++) {
            auto b = data[i];
            crc = (((crc >> 4) ^ (tbl[(b ^ crc) & 0x0F])) >> 4) ^ tbl[(((crc >> 4) ^ (tbl[(b ^ crc) & 0x0F])) ^ (b >> 4)) & 0x0F];
        }

        return crc;
    }

    inline uint8_t crc7_lgp_48(uint8_t crc, const uint8_t *data, size_t len) {
        static constexpr uint8_t tbl[] = { 
            0x00, 0x09, 0x12, 0x1B, 
            0x24, 0x2D, 0x36, 0x3F, 
            0x48, 0x41, 0x5A, 0x53, 
            0x6C, 0x65, 0x7E, 0x77 
        };

        crc &= 127;
        for (size_t i = 0; i < len; i++) {
            auto b = data[i];
            crc = (((crc >> 4) ^ (tbl[(b ^ crc) & 0x0F])) >> 4) ^ tbl[(((crc >> 4) ^ (tbl[(b ^ crc) & 0x0F])) ^ (b >> 4)) & 0x0F];
        }

        return crc;
    }
}