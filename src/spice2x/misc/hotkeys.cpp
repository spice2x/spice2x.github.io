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

        constexpr auto SAMPLE_INTERVAL = std::chrono::milliseconds(8);

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

        void run(std::stop_token stop_token) {
            bool screenshot_previous = false;
            bool coin_previous = false;
            bool super_exit_previous = false;
            bool alt_f4_previous = false;

            while (!stop_token.stop_requested()) {
                bool coin_edge = false;
                bool super_exit_edge = false;
                bool alt_f4_current = false;

                {
                    // lifecycle functions hold this mutex until raw-input polling is complete
                    std::lock_guard<std::mutex> lock(INPUT_MUTEX);
                    auto *buttons = games::get_buttons_overlay(eamuse_get_game());
                    bool screenshot_down = false;
                    bool super_exit_down = false;

                    if (INPUT_ENABLED) {
                        screenshot_down = read_button(
                                buttons, games::OverlayButtons::Screenshot);
                        const bool coin_current = read_button(
                                buttons, games::OverlayButtons::InsertCoin);
                        coin_edge = coin_current && !coin_previous;
                        coin_previous = coin_current;
                    } else {
                        coin_previous = false;
                    }

                    if (RAW_INPUT_ENABLED) {
                        super_exit_down = read_button(
                                buttons, games::OverlayButtons::SuperExit);
                    }

                    // gate polling mutates a shared edge latch, so avoid its lock chain while idle
                    const bool gate_active = (screenshot_down || super_exit_down) &&
                        overlay::global_hotkeys_triggered();
                    const bool screenshot_current = screenshot_down && gate_active;
                    const bool super_exit_current = super_exit_down && gate_active;

                    if (screenshot_current && !screenshot_previous) {
                        SCREENSHOT_PENDING.store(true, std::memory_order_relaxed);
                    }
                    super_exit_edge = super_exit_current && !super_exit_previous;
                    screenshot_previous = screenshot_current;
                    super_exit_previous = super_exit_current;

                    if (!INPUT_ENABLED) {
                        screenshot_previous = false;
                    }
                    if (!RAW_INPUT_ENABLED) {
                        super_exit_previous = false;
                    }

                    alt_f4_current = read_alt_f4();
                }

                if (coin_edge) {
                    eamuse_coin_insert();
                }

                const bool alt_f4_edge = alt_f4_current && !alt_f4_previous;
                alt_f4_previous = alt_f4_current;
                superexit::handle_hotkeys(alt_f4_edge, super_exit_edge);

                std::this_thread::sleep_for(SAMPLE_INTERVAL);
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
