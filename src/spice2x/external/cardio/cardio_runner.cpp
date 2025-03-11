#include "cardio_runner.h"
#include <thread>
#include <mutex>
#include "misc/eamuse.h"
#include "util/logging.h"
#include "cardio_hid.h"
#include "cardio_window.h"

bool CARDIO_RUNNER_FLIP = false;
bool CARDIO_RUNNER_TOGGLE = false;
static bool CARDIO_RUNNER_INITIALIZED = false;
static std::thread* CARDIO_RUNNER_THREAD = nullptr;
static HWND CARDIO_RUNNER_HWND = NULL;

void cardio_runner_start(bool scan_hid) {

    // initialize
    if (!CARDIO_RUNNER_INITIALIZED) {
        CARDIO_RUNNER_INITIALIZED = true;
        log_info("cardio", "Initializing CARDIO");

        // initialize
        if (!cardio_window_init()) {
            log_warning("cardio", "Couldn't init CARDIO window");
            return;
        }
        if (!cardio_hid_init()) {
            log_warning("cardio", "Couldn't init CARDIO HID");
            return;
        }

        // scan HID devices
        if (scan_hid) {
            if (!cardio_hid_scan()) {
                log_warning("cardio", "Couldn't scan for CARDIO devices");
                return;
            }
        }
    }

    // create thread
    if (CARDIO_RUNNER_THREAD == nullptr) {
        CARDIO_RUNNER_THREAD = new std::thread([] {

            // create window
            if (CARDIO_RUNNER_HWND == NULL) {
                if ((CARDIO_RUNNER_HWND = cardio_window_create(GetModuleHandle(NULL))) == NULL) {
                    log_warning("cardio", "Couldn't create CARDIO window");
                    return;
                }
            }

            // main loop
            while (CARDIO_RUNNER_HWND != NULL) {

                // update window
                cardio_window_update(CARDIO_RUNNER_HWND);

                // update HID devices
                EnterCriticalSection(&CARDIO_HID_CRIT_SECTION);
                for (size_t device_no = 0; device_no < CARDIO_HID_CONTEXTS_LENGTH; device_no++) {
                    auto device = &CARDIO_HID_CONTEXTS[device_no];

                    // get status
                    auto status = cardio_hid_device_poll(device);
                    if (status == HID_POLL_CARD_READY) {

                        // read card
                        if (cardio_hid_device_read(device) == HID_CARD_NONE)
                            continue;

                        // if card not empty
                        if (*((uint64_t*) &device->usage_value[0]) > 0) {

                            bool flip_order = CARDIO_RUNNER_FLIP;
                            if (CARDIO_RUNNER_FLIP) {
                                log_info("cardio", "Flip order of readers since flip option is set");
                            }
                            if (CARDIO_RUNNER_TOGGLE && (GetKeyState(VK_NUMLOCK) & 1) > 0) {
                                log_info("cardio", "Flip order of readers since Num Lock is on");
                                flip_order = !flip_order;
                            }

                            // insert card
                            if (flip_order)
                                eamuse_card_insert((int) (device_no + 1) & 1, &device->usage_value[0]);
                            else
                                eamuse_card_insert((int) device_no & 1, &device->usage_value[0]);
                        }
                    }
                }
                LeaveCriticalSection(&CARDIO_HID_CRIT_SECTION);

                // slow down
                Sleep(15);
            }
        });
    }
}

void cardio_runner_stop() {

    // destroy window
    cardio_window_close(CARDIO_RUNNER_HWND);
    CARDIO_RUNNER_HWND = NULL;
    cardio_window_shutdown();

    // destroy thread
    delete CARDIO_RUNNER_THREAD;
    CARDIO_RUNNER_THREAD = nullptr;

    // shutdown HID
    cardio_hid_close();

    // set initialized to false
    CARDIO_RUNNER_INITIALIZED = false;
}
