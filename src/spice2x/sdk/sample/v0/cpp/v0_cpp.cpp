#include <chrono>
#include <thread>

#include "sdk/include/spicesdk.h"
#include "sdk/include/spicesdk_io.h"

static SPICE_SDK_V0 spice = {};
static spice_sdk_destroy_callback_func destroy_callback;
static std::jthread worker_thread;

static void worker_thread_main(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {

        static bool state = false;
        bool new_state;
        spice.get_button(IIDX_Button_P1_Start, &new_state, nullptr);
        if (!state && new_state) {
            spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0_cpp", "user pressed P1 Start button!");
        }
        state = new_state;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0_cpp", "plugin loaded");
    worker_thread = std::jthread(worker_thread_main);
    return 1;
}

void
__cdecl
destroy_callback(
    void
)
{
    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0_cpp", "plugin unloading");
    if (worker_thread.joinable()) {
        worker_thread.request_stop();
        worker_thread.join();
    }
}
