#include "capture_pump.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
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
            // every subscriber's requested rate; the pump runs at the highest of them
            std::multiset<int> rates;
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
                        return SHUTDOWN.load() || !pump.rates.empty();
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

        this->rate = std::clamp<int>(fps, PUMP_FPS_MIN, PUMP_FPS_MAX);

        auto &pump = PUMPS[this->screen];
        std::lock_guard<std::mutex> lock(pump.m);

        // the pump captures at the fastest subscriber's rate; next() paces each one to its own
        pump.rates.insert(this->rate);
        pump.fps = *pump.rates.rbegin();

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

        const auto pos = pump.rates.find(this->rate);
        if (pos != pump.rates.end()) {
            pump.rates.erase(pos);
        }

        if (pump.rates.empty()) {
            pump.latest = nullptr;
        } else {
            pump.fps = *pump.rates.rbegin();
        }
    }

    FramePtr Subscription::next(unsigned int timeout_ms) {
        if (this->screen < 0) {
            return nullptr;
        }

        auto &pump = PUMPS[this->screen];
        std::unique_lock<std::mutex> lock(pump.m);

        const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (true) {
            const bool ready = pump.cv_frame.wait_until(
                    lock,
                    deadline,
                    [this, &pump] {
                        return SHUTDOWN.load() || (pump.latest && pump.seq != this->last_seq);
                    });

            if (!ready || SHUTDOWN.load()) {
                return nullptr;
            }

            this->last_seq = pump.seq;

            // the pump captures at the fastest subscriber's rate, so a slower one takes every
            // nth frame instead of making its encoder pay for frames it never asked for.
            // whole steps mean a rate that does not divide the pump's is rounded up to one
            // that does: never below what was asked for, and at worst about twice it while a
            // faster client is connected. exact pacing is not worth the code for that case
            const auto step = static_cast<uint64_t>(std::max(1, pump.fps / this->rate));
            if (step > 1 && (pump.seq % step) != 0) {
                continue;
            }

            return pump.latest;
        }
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
                pump.rates.clear();
                pump.latest = nullptr;
            }
            if (thread.joinable()) {
                thread.join();
            }
        }

        SHUTDOWN.store(false);
    }
}
