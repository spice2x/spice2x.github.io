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
#include "util/logging.h"

#define PERIOD 1
#define TOLERANCE 0.02

namespace timeutils {

    bool TIMER_HACKS_DISABLE = false;

    static PreciseTimer get_precise_sleep_timer();
    static void destroy_precise_sleep_timer(PreciseTimer timer);
    static void precise_sleep(PreciseTimer timer, double seconds);

    void set_timer_resolution() {
        if (TIMER_HACKS_DISABLE) {
            return;
        }

#if !SPICE_XP

        // make a call to opt out of power throttling
        // (requested timer resolution being ignored when window is occuluded or minimized)
        // SetProcessInformation is win8+
        const auto kernel32 = LoadLibraryA("kernel32.dll");
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
                    "sysutils",
                    "SetProcessInformation called to disable timer resolution throttling, returned {}",
                    ret);
            }
        }

#endif

        const auto ret = timeBeginPeriod(PERIOD);
        log_info(
            "sysutils",
            "timeBeginPeriod({}) called, returned {}",
            PERIOD,
            ret);
        // and then we never call timeEndPeriod();
        // the game will also call this but the OS will honor the lowest value (1 above)
    }

    // timerSleep from https://blog.bearcats.nl/perfect-sleep-function/
    static void timerSleep(HANDLE timer, double seconds) {
        using namespace std::chrono;

        auto t = high_resolution_clock::now();
        const auto target = t + nanoseconds(int64_t(seconds * 1e9));

        if (!timer) {
            timer = CreateWaitableTimerExW(
                NULL,
                NULL,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                TIMER_ALL_ACCESS);
        }

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

    static PreciseTimer get_precise_sleep_timer() {
        PreciseTimer timer = nullptr;
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

    static void destroy_precise_sleep_timer(PreciseTimer timer) {
        if (timer) {
            CloseHandle(timer);
        }
    }

    static void precise_sleep(PreciseTimer timer, double seconds) {
        if (timer) {
            timerSleep(timer, seconds);
            return;
        }

        // robustSleep is too CPU heavy so we will stick to Sleep() with
        // timeBeginPeriod, which has an error at most 1ms
        Sleep(static_cast<DWORD>(std::ceil(seconds * 1000.0)));
    }

    // RAII wrapper

    PreciseSleepTimer::PreciseSleepTimer() : timer(get_precise_sleep_timer()) {}

    PreciseSleepTimer::~PreciseSleepTimer() {
        destroy_precise_sleep_timer(timer);
    }

    PreciseSleepTimer::PreciseSleepTimer(PreciseSleepTimer &&other) noexcept :
        timer(other.timer) {
        other.timer = nullptr;
    }

    PreciseSleepTimer &PreciseSleepTimer::operator=(PreciseSleepTimer &&other) noexcept {
        if (this != &other) {
            destroy_precise_sleep_timer(timer);
            timer = other.timer;
            other.timer = nullptr;
        }

        return *this;
    }

    void PreciseSleepTimer::sleep(uint32_t ms) const {
        sleep(std::chrono::milliseconds(ms));
    }

    void PreciseSleepTimer::sleep(std::chrono::milliseconds ms) const {
        precise_sleep(timer, std::chrono::duration<double>(ms).count());
    }

    void PreciseSleepTimer::sleep(std::chrono::microseconds us) const {
        precise_sleep(timer, std::chrono::duration<double>(us).count());
    }
}
