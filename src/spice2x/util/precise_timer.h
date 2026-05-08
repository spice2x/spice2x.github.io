#pragma once

#include <chrono>
#include <cstdint>

namespace timeutils {

    extern bool TIMER_HACKS_DISABLE;

    void set_timer_resolution();

    using PreciseTimer = void *;

    class PreciseSleepTimer {
    public:
        PreciseSleepTimer();
        ~PreciseSleepTimer();

        PreciseSleepTimer(const PreciseSleepTimer &) = delete;
        PreciseSleepTimer &operator=(const PreciseSleepTimer &) = delete;

        PreciseSleepTimer(PreciseSleepTimer &&other) noexcept;
        PreciseSleepTimer &operator=(PreciseSleepTimer &&other) noexcept;

        void sleep(uint32_t ms) const;
        void sleep(std::chrono::milliseconds ms) const;
        void sleep(std::chrono::microseconds us) const;

    private:
        PreciseTimer timer;
    };
}
