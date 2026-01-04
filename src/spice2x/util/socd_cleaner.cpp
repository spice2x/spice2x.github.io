#include "socd_cleaner.h"

#include "util/logging.h"

namespace socd {
    
    SocdAlgorithm ALGORITHM = SocdAlgorithm::PreferRecent;
   
    static double last_rising_edge[2][2] = {};
    static bool last_button_state[2][2] = {};

    // this has no locking, use only if you know there is only one i/o thread that will call this
    SocdResult socd_clean(uint8_t device, bool ccw, bool cw, double time_now) {
        if (device >= 2) {
            log_fatal("socd", "invalid device index in socd_clean: {}", device);
        }

        // SOCD last input algorithm needs to keep track of rising edge times
        if (ALGORITHM != SocdAlgorithm::Neutral) {
            // detect rising edges
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
        if (ALGORITHM == SocdAlgorithm::PreferRecent) {
            // SOCD: prefer last input
            if (ccw_time < cw_time) {
                // while CCW is being held, CW got pressed
                return SocdCW;
            } else if (ccw_time > cw_time) {
                // while CW is being held, CCW got pressed
                return SocdCCW;
            } else {
                // it's a tie
                return SocdNone;
            }
        } else if (ALGORITHM == SocdAlgorithm::PreferFirst) {
            // SOCD: keep first input
            if (ccw_time < cw_time) {
                // while CCW is being held, CW got pressed
                return SocdCCW;
            } else if (ccw_time > cw_time) {
                // while CW is being held, CCW got pressed
                return SocdCW;
            } else {
                // it's a tie
                return SocdNone;
            }
        } else {
            // SOCD: neutral when both are pressed
            return SocdNone;
        }
    }
}
