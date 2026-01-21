#include "socd_cleaner.h"

#include "util/logging.h"

#define DEBUG_VERBOSE 0

#if DEBUG_VERBOSE
#define log_debug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_debug(module, format_str, ...)
#endif

namespace socd {

    SocdAlgorithm ALGORITHM = SocdAlgorithm::Neutral;
   
    static double last_rising_edge[4][2] = {};
    static bool last_button_state[4][2] = {};

    // this has no locking, use only if you know there is only one i/o thread that will call this
    SocdResult socd_clean(
        uint8_t device, bool ccw, bool cw, double time_now,
        std::optional<SocdAlgorithm> algorithm_override) {

        if (device >= 4) {
            log_fatal("socd", "invalid device index in socd_clean: {}", device);
        }

        SocdAlgorithm algo =
            algorithm_override.has_value() ? algorithm_override.value() : ALGORITHM;

        // keep track of rising edge times
        if (algo == SocdAlgorithm::PreferRecent || algo == SocdAlgorithm::PreferFirst) {
            if (!last_button_state[device][SocdCCW] && ccw) {
                last_rising_edge[device][SocdCCW] = time_now;
            }
            if (!last_button_state[device][SocdCW] && cw) {
                last_rising_edge[device][SocdCW] = time_now;
            }

            // update button state for next time
            last_button_state[device][SocdCCW] = ccw;
            last_button_state[device][SocdCW] = cw;
        }

        // determine direction: easy cases
        if (!ccw && !cw) {
            return SocdNone;
        }
        if (ccw && !cw) {
            return SocdCCW;
        }
        if (!ccw && cw) {
            return SocdCW;
        }

        // SOCD logic; depends on algorithm in use
        const auto ccw_time = last_rising_edge[device][SocdCCW];
        const auto cw_time = last_rising_edge[device][SocdCW];
        log_debug("socd", "ccw={}, cw ={}", ccw_time, cw_time);

        if (algo == SocdAlgorithm::PreferRecent) {
            // SOCD: prefer last input
            if (ccw_time < cw_time) {
                // while CCW is being held, CW got pressed
                return SocdCW;
            } else if (ccw_time > cw_time) {
                // while CW is being held, CCW got pressed
                return SocdCCW;
            } else {
                // it's a tie; instead of none, we'll pick a direction
                return SocdCW;
            }
        } else if (algo == SocdAlgorithm::PreferFirst) {
            // SOCD: keep first input
            if (ccw_time < cw_time) {
                // while CCW is being held, CW got pressed
                return SocdCCW;
            } else if (ccw_time > cw_time) {
                // while CW is being held, CCW got pressed
                return SocdCW;
            } else {
                // it's a tie; instead of none, we'll pick a direction
                return SocdCW;
            }
        } else if (algo == SocdAlgorithm::Neutral) {
            // SOCD: neutral when both are pressed
            return SocdNone;
        } else if (algo == SocdAlgorithm::None) {
            // SOCD: both are pressed
            return SocdBoth;
        } else {
            log_fatal("socd", "invalid SOCD algorithm");
            return SocdNone;
        }
    }

    // first dimension: p1/p2
    // second dimension: TiltUp / TiltDown
    // value: last timestamp when it was on
    static double most_recent_active[2][2] = {};

    uint32_t TILT_HOLD_MS = 100;

    TiltResult get_guitar_wail(uint8_t device, bool up, bool down, double time_now) {
        if (device >= 2) {
            log_fatal("socd", "invalid device index in socd_clean: {}", device);
        }

        const auto socd_result = socd_clean(
                device + 2, up, down, time_now, SocdAlgorithm::PreferRecent);

        if (up) {
            most_recent_active[device][TiltUp] = time_now;
        }
        if (down) {
            most_recent_active[device][TiltDown] = time_now;
        }

        log_debug(
            "socd",
            "p{} wail up={}, down={}",
            device + 1,
            most_recent_active[device][TiltUp],
            most_recent_active[device][TiltDown]);

        const auto delta_up = time_now - most_recent_active[device][TiltUp];
        const auto delta_down = time_now - most_recent_active[device][TiltDown];
        const bool is_up = delta_up <= TILT_HOLD_MS;
        const bool is_down = delta_down <= TILT_HOLD_MS;

        auto result = TiltNone;
        if (is_up && is_down) {
            // both held recently: prefer more recently held using SOCD logic
            if (socd_result == SocdCCW) {
                result = TiltUp;
            } else if (socd_result == SocdCW) {
                result = TiltDown;
            } else if (socd_result == SocdBoth) {
                result = TiltUp;
            }
        } else if (is_up) {
            result = TiltUp;
        } else if (is_down) {
            result = TiltDown;
        }

        // clear opposite direction being held
        if (result == TiltUp) {
            most_recent_active[device][TiltDown] = 0.f;
        } else if (result == TiltDown) {
            most_recent_active[device][TiltUp] = 0.f;
        }

        return result;
    }
}
