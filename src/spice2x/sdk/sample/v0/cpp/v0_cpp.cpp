#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "sdk/include/spicesdk.h"
#include "sdk/include/spicesdk_io.h"

static SPICE_SDK_V0 spice = {};
static spice_sdk_destroy_callback_func destroy_callback;
static std::atomic_bool worker_thread_stop = false;
static std::thread worker_thread;

static void worker_thread_main() {
    while (!worker_thread_stop.load()) {
        spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0_cpp", "ping!");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

__declspec(dllexport)
int
__cdecl
spice_sdk_entry_point(
    spice_sdk_init_func *init
)
{
    SPICE_SDK_STATUS_CODE status;

    spice.size = sizeof(spice);
    status = init(0, destroy_callback, &spice);
    if (status != SPICE_SDK_STATUS_SUCCESS) {
        return 0;
    }

    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "plugin loaded");
    worker_thread_stop.store(false);
    worker_thread = std::thread(worker_thread_main);
    return 1;
}

void
__cdecl
destroy_callback(
    void
)
{
    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "plugin unloading");
    worker_thread_stop.store(true);
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}
