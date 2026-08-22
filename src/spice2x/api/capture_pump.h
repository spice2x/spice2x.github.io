#pragma once

#include <cstdint>
#include <memory>

namespace api::capture_pump {

    struct Frame {
        // packed 24bpp RGB, width * height * 3 bytes
        std::shared_ptr<uint8_t[]> pixels;
        uint64_t timestamp = 0;
        int width = 0;
        int height = 0;
    };

    // the graphics layer has one capture slot per screen, so concurrent waiters would steal
    // each other's frames; everything that captures goes through here to keep it serialized
    bool capture_direct(int screen, std::shared_ptr<uint8_t[]> &out, int divide,
            uint64_t *timestamp = nullptr, int *width = nullptr, int *height = nullptr);

    // a screen carries one stream at a time; false when another connection already holds it
    bool claim_screen(int screen);
    void release_screen(int screen);

    // true while a video stream client holds this screen
    bool screen_claimed(int screen);
}
