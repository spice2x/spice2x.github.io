#pragma once

#include <cstdint>

namespace socd {

    enum class SocdAlgorithm {
        Neutral,
        PreferRecent,
        PreferFirst
    };

    extern SocdAlgorithm ALGORITHM;

    typedef enum _SocdResult {
        SocdCCW = 0,
        SocdCW = 1,
        SocdNone = 2
    } SocdResult;

    SocdResult socd_clean(uint8_t device, bool ccw, bool cw, double time_now);
}