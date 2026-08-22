#include "capture_pump.h"

#include <array>
#include <atomic>
#include <mutex>

#include "hooks/graphics/graphics.h"

namespace api::capture_pump {

    namespace {

        std::array<std::mutex, GRAPHICS_CAPTURE_SCREEN_NO> CONSUMER_M;

        // read once per capture from the present thread, so it stays lock free
        std::atomic<bool> CLAIMED[GRAPHICS_CAPTURE_SCREEN_NO] {};

        bool valid_screen(int screen) {
            return 0 <= screen && screen < static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO);
        }
    }

    bool capture_direct(int screen, std::shared_ptr<uint8_t[]> &out, int divide,
            uint64_t *timestamp, int *width, int *height) {

        if (!valid_screen(screen)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(CONSUMER_M[screen]);
        graphics_capture_trigger(screen);
        return graphics_capture_receive_raw(
                screen, out, divide, timestamp, width, height);
    }

    bool claim_screen(int screen) {
        if (!valid_screen(screen)) {
            return false;
        }

        return !CLAIMED[screen].exchange(true);
    }

    void release_screen(int screen) {
        if (!valid_screen(screen)) {
            return;
        }

        CLAIMED[screen] = false;
    }

    bool screen_claimed(int screen) {
        if (!valid_screen(screen)) {
            return false;
        }

        return CLAIMED[screen];
    }
}
