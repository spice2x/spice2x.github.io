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

    // the graphics layer has one capture slot per screen, so concurrent waiters would steal
    // each other's frames; everything that captures goes through here to keep it serialized

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

        // waits for the next frame due at this subscription's own rate. slow callers skip
        // whatever they missed instead of falling behind
        FramePtr next(unsigned int timeout_ms);

    private:
        int screen;
        int rate = 0;
        double interval_ms = 0;
        double next_due = 0;
        uint64_t last_seq = 0;
    };

    void shutdown();
}
