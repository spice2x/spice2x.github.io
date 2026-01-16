#pragma once

#include <cstdint>

namespace socd {

    // SOCD for knobs / turntables

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

    // for guitar wail (up/down only)

    extern uint32_t TILT_HOLD_MS;

    typedef enum _TiltResult {
        TiltUp = 0,
        TiltDown = 1,
        TiltNone = 2
    } TiltResult;

    TiltResult get_guitar_wail(uint8_t device, bool up, bool down, double time_now);
}