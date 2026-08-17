#include "hotkeys.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include <windows.h>

#include "games/io.h"
#include "launcher/superexit.h"
#include "misc/eamuse.h"
#include "overlay/overlay.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"

namespace hotkeys {

    namespace {

        // 8 ms targets short screenshot pulses; sleep_for may use a coarser scheduler
        // interval during early boot or when process timer adjustments are disabled
        constexpr auto MIN_SAMPLE_INTERVAL = std::chrono::milliseconds(8);

        std::atomic_bool SCREENSHOT_PENDING {false};
        std::mutex INPUT_MUTEX;
        bool INPUT_ENABLED = false;
        bool RAW_INPUT_ENABLED = false;
        std::jthread WORKER;

        bool read_button(std::vector<Button> *buttons, size_t index) {
            // getState retains each binding's focus, modifier, inversion, and debounce policy
            return RI_MGR && buttons && index < buttons->size() &&
                GameAPI::Buttons::getState(RI_MGR, buttons->at(index));
        }

        bool read_alt_f4() {
            // preserve both legacy detection paths whenever raw input is available
            bool pressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 &&
                (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            if (!RAW_INPUT_ENABLED || !RI_MGR) {
                return pressed;
            }
            return pressed || RI_MGR->keyboard_combo_pressed(VK_MENU, VK_F4);
        }

        bool rising_edge(bool current, bool &previous) {
            const bool edge = current && !previous;
            previous = current;
            return edge;
        }

        void run(std::stop_token stop_token) {
            bool screenshot_previous = false;
            bool coin_previous = false;

            while (!stop_token.stop_requested()) {
                bool coin_edge = false;
                bool super_exit_current = false;
                bool alt_f4_current = false;

                {
                    // lifecycle functions hold this mutex until raw-input polling is complete
                    std::lock_guard<std::mutex> lock(INPUT_MUTEX);
                    if (INPUT_ENABLED || RAW_INPUT_ENABLED) {
                        auto *buttons = games::get_buttons_overlay(eamuse_get_game());
                        const bool screenshot_down = INPUT_ENABLED && read_button(
                            buttons, games::OverlayButtons::Screenshot);
                        const bool coin_current = INPUT_ENABLED && read_button(
                                buttons, games::OverlayButtons::InsertCoin);
                        const bool super_exit_down = RAW_INPUT_ENABLED && read_button(
                                buttons, games::OverlayButtons::SuperExit);

                        // global_hotkeys_triggered takes OVERLAY_MUTEX, then the overlay's
                        // hotkeys_mutex; its button reads may then take device mutexes. polling
                        // every mapped-input tick is intentional so HotkeyToggle releases cannot
                        // be missed when the render thread stalls.
                        const bool gate_active = overlay::global_hotkeys_triggered();
                        const bool screenshot_current = screenshot_down && gate_active;
                        super_exit_current = super_exit_down && gate_active;

                        if (rising_edge(screenshot_current, screenshot_previous)) {
                            SCREENSHOT_PENDING.store(true, std::memory_order_relaxed);
                        }
                        coin_edge = rising_edge(coin_current, coin_previous);
                    } else {
                        screenshot_previous = false;
                        coin_previous = false;
                    }

                    alt_f4_current = read_alt_f4();
                }

                if (coin_edge) {
                    eamuse_coin_insert();
                }

                // pass held state so returning focus can exit without another key press
                superexit::handle_hotkeys(alt_f4_current, super_exit_current);

                std::this_thread::sleep_for(MIN_SAMPLE_INTERVAL);
            }
        }
    }

    void start() {
        if (WORKER.joinable()) {
            return;
        }

        SCREENSHOT_PENDING.store(false, std::memory_order_relaxed);
        WORKER = std::jthread(run);
        log_info("hotkeys", "sampler started");
    }

    void enable_raw_input() {
        std::lock_guard<std::mutex> lock(INPUT_MUTEX);
        RAW_INPUT_ENABLED = true;
    }

    void enable_input() {
        std::lock_guard<std::mutex> lock(INPUT_MUTEX);
        SCREENSHOT_PENDING.store(false, std::memory_order_relaxed);
        INPUT_ENABLED = true;
        log_info("hotkeys", "configured input enabled");
    }

    void disable_input() {
        std::lock_guard<std::mutex> lock(INPUT_MUTEX);
        INPUT_ENABLED = false;
        SCREENSHOT_PENDING.store(false, std::memory_order_relaxed);
        log_info("hotkeys", "configured input disabled");
    }

    void disable_raw_input() {
        std::lock_guard<std::mutex> lock(INPUT_MUTEX);
        RAW_INPUT_ENABLED = false;
    }

    void stop() {
        if (!WORKER.joinable()) {
            return;
        }
        WORKER.request_stop();
        WORKER.join();
        log_info("hotkeys", "sampler stopped");
    }

    bool consume_screenshot() {
        return SCREENSHOT_PENDING.exchange(false, std::memory_order_relaxed);
    }
}
