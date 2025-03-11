#include "time.h"

#include <chrono>

#include <windows.h>

#include "util/logging.h"

using namespace std::chrono;

static bool PC_INITIALIZED = false;
static double PC_FREQUENCY = 3000000000;
static LARGE_INTEGER PC_START {};

void init_performance_counter() {

    // initialize performance counter
    if (!PC_INITIALIZED) {

        // query frequency
        LARGE_INTEGER frequency {};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
            log_warning("time", "unable to get performance counter frequency, defaulting to 3GHz");
            PC_FREQUENCY = 3000000000;
        } else {
            log_info("time", "detected performance counter frequency: {}", frequency.QuadPart);
            PC_FREQUENCY = static_cast<double>(frequency.QuadPart);
        }

        // get start frequency
        QueryPerformanceCounter(&PC_START);

        // mark as initialized
        PC_INITIALIZED = true;
    }
}

double get_performance_seconds() {
    if (!PC_INITIALIZED) {
      init_performance_counter();
    }
    LARGE_INTEGER time_now;
    QueryPerformanceCounter(&time_now);
    LONGLONG time_diff = time_now.QuadPart - PC_START.QuadPart;
    return static_cast<double>(time_diff) / PC_FREQUENCY;
}

double get_performance_milliseconds() {
    return get_performance_seconds() * 1000.0;
}

uint64_t get_system_seconds() {
    auto sec = duration_cast<seconds>(system_clock::now().time_since_epoch());
    return sec.count();
}

uint64_t get_system_milliseconds() {
    auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}
