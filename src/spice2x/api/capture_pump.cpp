#include "capture_pump.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "hooks/graphics/graphics.h"
#include "util/logging.h"

namespace api::capture_pump {

    namespace {

        constexpr unsigned int PUMP_FPS_MIN = 1;
        constexpr unsigned int PUMP_FPS_MAX = 240;

        struct PumpState {
            std::mutex m;
            std::condition_variable cv_idle;
            std::condition_variable cv_frame;
            FramePtr latest;
            uint64_t seq = 0;
            int subscribers = 0;
            int fps = 30;
            bool started = false;
            std::thread thread;
        };

        std::array<PumpState, GRAPHICS_CAPTURE_SCREEN_NO> PUMPS;
        std::array<std::mutex, GRAPHICS_CAPTURE_SCREEN_NO> CONSUMER_M;

        std::mutex SHUTDOWN_M;
        std::atomic_bool SHUTDOWN { false };

        bool valid_screen(int screen) {
            return 0 <= screen && screen < static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO);
        }

        void pump_worker(int screen) {
            auto &pump = PUMPS[screen];

            while (true) {
                int fps;

                {
                    std::unique_lock<std::mutex> lock(pump.m);
                    pump.cv_idle.wait(lock, [&pump] {
                        return SHUTDOWN.load() || pump.subscribers > 0;
                    });

                    if (SHUTDOWN.load()) {
                        return;
                    }

                    fps = pump.fps;
                }

                const auto started = std::chrono::steady_clock::now();

                uint64_t timestamp = 0;
                int width = 0;
                int height = 0;
                std::shared_ptr<uint8_t[]> pixels;
                const bool ok = capture_direct(
                        screen, pixels, 1, &timestamp, &width, &height);

                if (ok && pixels) {
                    auto frame = std::make_shared<Frame>();
                    frame->pixels = std::move(pixels);
                    frame->timestamp = timestamp;
                    frame->width = width;
                    frame->height = height;

                    std::lock_guard<std::mutex> lock(pump.m);
                    frame->seq = ++pump.seq;
                    pump.latest = std::move(frame);
                    pump.cv_frame.notify_all();
                }

                // a failed capture still paces, otherwise a stalled game spins this thread
                const auto deadline = started + std::chrono::microseconds(1000000 / fps);
                std::this_thread::sleep_until(deadline);
            }
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

    Subscription::Subscription(int screen, int fps)
        : screen(valid_screen(screen) ? screen : -1)
    {
        if (this->screen < 0 || SHUTDOWN.load()) {
            this->screen = -1;
            return;
        }

        auto &pump = PUMPS[this->screen];
        std::lock_guard<std::mutex> lock(pump.m);

        // the fastest subscriber sets the pace; everyone shares the same frames
        pump.fps = pump.subscribers == 0
                ? std::clamp<int>(fps, PUMP_FPS_MIN, PUMP_FPS_MAX)
                : std::max(pump.fps, std::clamp<int>(fps, PUMP_FPS_MIN, PUMP_FPS_MAX));

        pump.subscribers++;
        this->last_seq = pump.seq;

        if (!pump.started) {
            pump.started = true;
            pump.thread = std::thread(pump_worker, this->screen);
        }

        pump.cv_idle.notify_all();
    }

    Subscription::~Subscription() {
        if (this->screen < 0) {
            return;
        }

        auto &pump = PUMPS[this->screen];
        std::lock_guard<std::mutex> lock(pump.m);

        if (--pump.subscribers <= 0) {
            pump.subscribers = 0;
            pump.latest = nullptr;
        }
    }

    FramePtr Subscription::next(unsigned int timeout_ms) {
        if (this->screen < 0) {
            return nullptr;
        }

        auto &pump = PUMPS[this->screen];
        std::unique_lock<std::mutex> lock(pump.m);

        const auto ready = pump.cv_frame.wait_for(
                lock,
                std::chrono::milliseconds(timeout_ms),
                [this, &pump] {
                    return SHUTDOWN.load() || (pump.latest && pump.seq != this->last_seq);
                });

        if (!ready || SHUTDOWN.load()) {
            return nullptr;
        }

        this->last_seq = pump.seq;
        return pump.latest;
    }

    FramePtr latest(int screen) {
        if (!valid_screen(screen)) {
            return nullptr;
        }

        auto &pump = PUMPS[screen];
        std::lock_guard<std::mutex> lock(pump.m);

        if (pump.subscribers <= 0) {
            return nullptr;
        }

        return pump.latest;
    }

    void shutdown() {
        std::lock_guard<std::mutex> shutdown_lock(SHUTDOWN_M);

        SHUTDOWN.store(true);

        for (auto &pump : PUMPS) {
            std::lock_guard<std::mutex> lock(pump.m);
            pump.cv_idle.notify_all();
            pump.cv_frame.notify_all();
        }

        for (auto &pump : PUMPS) {
            std::thread thread;
            {
                std::lock_guard<std::mutex> lock(pump.m);
                thread = std::move(pump.thread);
                pump.started = false;
                pump.subscribers = 0;
                pump.latest = nullptr;
            }
            if (thread.joinable()) {
                thread.join();
            }
        }

        SHUTDOWN.store(false);
    }
}
