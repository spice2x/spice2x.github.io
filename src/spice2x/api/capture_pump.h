#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace api::capture_pump {

    struct Frame {
        // packed 24bpp RGB, width * height * 3 bytes
        std::shared_ptr<uint8_t[]> pixels;
        uint64_t timestamp = 0;
        int width = 0;
        int height = 0;
        uint64_t seq = 0;
    };

    using FramePtr = std::shared_ptr<const Frame>;

    /*
     * the graphics layer keeps a single capture slot per screen, so two threads waiting on it
     * steal each other's frames. everything that wants a captured frame goes through here:
     * either directly (one at a time, serialized per screen) or by subscribing to a pump that
     * captures on a timer and shares each frame with every subscriber.
     */

    // trigger + receive under the per-screen consumer lock
    bool capture_direct(int screen, std::shared_ptr<uint8_t[]> &out, int divide,
            uint64_t *timestamp = nullptr, int *width = nullptr, int *height = nullptr);

    // keeps a screen's pump running for as long as it exists
    class Subscription {
    public:
        Subscription(int screen, int fps);
        ~Subscription();

        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;

        // waits for a frame newer than the last one this subscription returned. slow callers
        // skip whatever they missed instead of falling behind
        FramePtr next(unsigned int timeout_ms);

    private:
        int screen;
        uint64_t last_seq = 0;
    };

    // most recent frame of a running pump, or null when no pump owns the screen
    FramePtr latest(int screen);

    void shutdown();
}
