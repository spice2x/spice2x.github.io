#define _WIN32_WINNT 0x0602

#include "precise_timer.h"

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include <timeapi.h>

#if !SPICE_XP
#include <processthreadsapi.h>
#endif

#include <chrono>
#include <cmath>
#include <limits>
#include "util/logging.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#define PERIOD 1
#define TOLERANCE 0.02

#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

namespace timeutils {

    bool TIMER_HACKS_DISABLE = false;

    static TimerHandle get_precise_sleep_timer();
    static void destroy_precise_sleep_timer(TimerHandle timer);
    static void precise_sleep(TimerHandle timer, double seconds);

    void set_timer_resolution() {
        if (TIMER_HACKS_DISABLE) {
            return;
        }

#if !SPICE_XP

        // make a call to opt out of power throttling
        // (requested timer resolution being ignored when window is occluded or minimized)
        // SetProcessInformation is win8+
        const auto kernel32 = GetModuleHandleA("kernel32.dll");
        if (kernel32) {
            const auto SetProcessInformation_addr =
                reinterpret_cast<decltype(SetProcessInformation) *>(
                    GetProcAddress(kernel32, "SetProcessInformation"));

            if (SetProcessInformation_addr) {
                PROCESS_POWER_THROTTLING_STATE state {};
                state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
                state.StateMask = 0;
                const auto ret = SetProcessInformation_addr(
                    GetCurrentProcess(),
                    ProcessPowerThrottling,
                    &state,
                    sizeof(state));

                log_info(
                    "timeutils",
                    "SetProcessInformation called to disable timer resolution throttling, returned {}",
                    ret);
            }
        }

#endif

        const auto ret = timeBeginPeriod(PERIOD);
        log_info(
            "timeutils",
            "timeBeginPeriod({}) called, returned {}",
            PERIOD,
            ret);
        // and then we never call timeEndPeriod();
        // the game will also call this but the OS will honor the lowest value (1 above)
    }

#if !SPICE_XP

    // timerSleep from https://blog.bearcats.nl/perfect-sleep-function/
    static void timerSleep(HANDLE timer, double seconds) {
        using namespace std::chrono;

        auto t = high_resolution_clock::now();
        const auto target = t + nanoseconds(int64_t(seconds * 1e9));
        const int64_t maxTicks = PERIOD * 9'500;
        for (;;) {
            const int64_t remaining = (target - t).count();
            int64_t ticks = (remaining - TOLERANCE) / 100;
            if (ticks <= 0) {
                break;
            }
            if (ticks > maxTicks) {
                ticks = maxTicks;
            }

            LARGE_INTEGER due;
            due.QuadPart = -ticks;
            SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 0);
            WaitForSingleObject(timer, INFINITE);
            t = high_resolution_clock::now();
        }

        while (high_resolution_clock::now() < target) {
            YieldProcessor();
        }
    }

#endif

    static TimerHandle get_precise_sleep_timer() {
        TimerHandle timer = nullptr;
        if (TIMER_HACKS_DISABLE) {
            return timer;
        }

        // CreateWaitableTimerExW is Vista+
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION win10 1803+

#if !SPICE_XP

        timer = CreateWaitableTimerExW(
            NULL,
            NULL,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);

#endif

        return timer;
    }

    static void destroy_precise_sleep_timer(TimerHandle timer) {
        if (timer) {
            CloseHandle(timer);
        }
    }

    static void precise_sleep(TimerHandle timer, double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0.0) {
            return;
        }

#if !SPICE_XP
        if (timer) {
            timerSleep(timer, seconds);
            return;
        }
#endif

        // robustSleep is too CPU heavy so we will stick to Sleep() with
        // timeBeginPeriod, which has an error at most 1ms
        const auto milliseconds = std::ceil(seconds * 1000.0);
        if (milliseconds >= std::numeric_limits<DWORD>::max()) {
            Sleep(std::numeric_limits<DWORD>::max());
            return;
        }

        Sleep(static_cast<DWORD>(milliseconds));
    }

    // RAII wrapper

    PreciseSleepTimer::PreciseSleepTimer() : timer(get_precise_sleep_timer()) {}

    PreciseSleepTimer::~PreciseSleepTimer() {
        destroy_precise_sleep_timer(timer);
    }

    void PreciseSleepTimer::sleep(uint32_t ms) const {
        sleep(std::chrono::milliseconds(ms));
    }

    void PreciseSleepTimer::sleep_seconds(double seconds) const {
        precise_sleep(timer, seconds);
    }
}
