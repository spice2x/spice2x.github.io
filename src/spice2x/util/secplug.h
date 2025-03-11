#pragma once

#include <cstdint>
#include <array>

namespace secplug {
    constexpr std::array<uint8_t, 6> encode_secplug_model(const char model[8]) {
        // 6-bit character code similar to the one used by avs in its binary xml format.
        std::array<uint8_t, 6> packed {};
        packed[0] = ((model[0] - 32))      | ((model[1] - 32) << 6);
        packed[1] = ((model[1] - 32) >> 2) | ((model[2] - 32) << 4);
        packed[2] = ((model[2] - 32) >> 4) | ((model[3] - 32) << 2);
        packed[3] = ((model[4] - 32))      | ((model[5] - 32) << 6);
        packed[4] = ((model[5] - 32) >> 2) | ((model[6] - 32) << 4);
        packed[5] = ((model[6] - 32) >> 4) | ((model[7] - 32) << 2);

        return packed;
    }
};