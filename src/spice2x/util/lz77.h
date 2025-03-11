#pragma once

#include <vector>
#include <cstdint>

namespace util::lz77 {

    uint8_t *compress_stub(uint8_t *input, size_t input_length, size_t *compressed_length);
    std::vector<uint8_t> compress(uint8_t *input, size_t input_length);
    std::vector<uint8_t> decompress(uint8_t *input, size_t input_length);
}
