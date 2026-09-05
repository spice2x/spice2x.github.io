#include <format>
#include <chrono>
#include <thread>
#include <windows.h>

#include "sdk/include/spicesdk.h"
#include "sdk/include/spicesdk_io.h"

// this sample assumes that the game is DDR

// convenience wrapper for logging; feel free to add other levels
#define LOG_INFO(message) spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0_cpp", message)

static SPICE_SDK_V0 spice = {};

static spice_sdk_destroy_callback_func destroy_callback;
static std::jthread worker_thread;
static void worker_thread_main(std::stop_token stop_token);

// main entry point into the DLL
// spice executable will call into this shortly after the game is partially 
// initialized (but not quite fully booted just yet)
SPICE_SDK_ENTRY_POINT
spice_sdk_entry_point(
    spice_sdk_init_func *init
)
{
    SPICE_SDK_STATUS_CODE status;

    // SPICE_SDK_V0 must be zeroed AND the size field must be set
    spice.size = sizeof(spice);

    // initialize the SDK - can be called any time, but we'll do it here
    status = init(0, destroy_callback, &spice);

    // always check for return value; failure could be usage error,
    // or the spice executable doesn't support this SDK version
    // check the log messages from sdk:
    if (status != SPICE_SDK_STATUS_SUCCESS) {
        return 0;
    }

    LOG_INFO("plugin loaded");

    // spin up a worker thread
    worker_thread = std::jthread(worker_thread_main);
    return 1;
}

void
__cdecl
destroy_callback(
    void
)
{
    LOG_INFO("plugin unloading");
    if (worker_thread.joinable()) {
        worker_thread.request_stop();
        worker_thread.join();
    }
}

struct ArrowButton {
    int key;
    SPICE_SDK_DDR_BUTTONS button;
    const char* name;
    bool previous_state;
};

static ArrowButton arrow_buttons[] = {
    { VK_UP, DDR_Button_P1_PANEL_UP, "UP", false },
    { VK_DOWN, DDR_Button_P1_PANEL_DOWN, "DOWN", false },
    { VK_LEFT, DDR_Button_P1_PANEL_LEFT, "LEFT", false },
    { VK_RIGHT, DDR_Button_P1_PANEL_RIGHT, "RIGHT", false },
};

// worker thread for I/O
static void worker_thread_main(std::stop_token stop_token) {
    bool coin_previous_state[10] = {};
    while (!stop_token.stop_requested()) {

        // insert coin
        const bool control_pressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        for (uint8_t amount = 0; amount < 10; amount++) {
            const bool coin_pressed = control_pressed &&
                ((GetAsyncKeyState('0' + amount) & 0x8000) != 0);

            if (coin_pressed && !coin_previous_state[amount] && spice.insert_coin) {
                const auto status = spice.insert_coin(amount);
                if (status != SPICE_SDK_STATUS_SUCCESS) {
                    LOG_INFO(std::format(
                        "coin insertion failed: {}",
                        static_cast<int>(status)).c_str());
                } else {
                    const auto message = std::format(
                        "v0_cpp: inserted {} coin{}", amount, amount == 1 ? "" : "s");
                    LOG_INFO(message.c_str());
                    if (spice.add_toast) {
                        spice.add_toast(SPICE_SDK_TOAST_LEVEL_SUCCESS, message.c_str());
                    }
                }
            }
            coin_previous_state[amount] = coin_pressed;
        }

        // check for ctrl + arrow keys and trigger p1 pad arrows
        for (auto& arrow : arrow_buttons) {
            if (((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) &&
                ((GetAsyncKeyState(arrow.key) & 0x8000) != 0)) {
                spice.set_button(arrow.button, true, 1.f);

                if (!arrow.previous_state) {

                    if (spice.add_toast) {
                        spice.add_toast(
                            SPICE_SDK_TOAST_LEVEL_INFO,
                            std::format("v0_cpp: let me hear you say: {}", arrow.name).c_str());
                    } else {
                        LOG_INFO(std::format("let me hear you say: {}", arrow.name).c_str());
                    }

                    arrow.previous_state = true;
                }
            } else {
                spice.set_button(arrow.button, false, 0.f);
                arrow.previous_state = false;
            }
        }

        Sleep(1);
    }
}
