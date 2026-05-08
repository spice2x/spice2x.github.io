#pragma once

#include <chrono>
#include <cstdint>

namespace timeutils {

    extern bool TIMER_HACKS_DISABLE;

    void set_timer_resolution();

    using TimerHandle = void *;

    class PreciseSleepTimer {
    public:
        PreciseSleepTimer();
        ~PreciseSleepTimer();

        PreciseSleepTimer(const PreciseSleepTimer &) = delete;
        PreciseSleepTimer &operator=(const PreciseSleepTimer &) = delete;
        PreciseSleepTimer(PreciseSleepTimer &&) = delete;
        PreciseSleepTimer &operator=(PreciseSleepTimer &&) = delete;

        void sleep(uint32_t ms) const;

        template <typename Rep, typename Period>
        void sleep(std::chrono::duration<Rep, Period> duration) const {
            sleep_seconds(std::chrono::duration<double>(duration).count());
        }

    private:
        void sleep_seconds(double seconds) const;

        TimerHandle timer;
    };
}
