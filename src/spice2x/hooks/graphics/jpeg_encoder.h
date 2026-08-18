#pragma once

#include <cstdint>
#include <vector>

namespace jpeg_encoder {

    // appends a baseline 4:2:0 JPEG of packed 24bpp RGB pixels to `out`
    bool encode(
            std::vector<uint8_t> &out,
            const uint8_t *pixels,
            int width,
            int height,
            int quality);
}
