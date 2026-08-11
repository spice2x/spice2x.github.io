#include "hotkeys.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include <windows.h>

#include "games/io.h"
#include "launcher/superexit.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"

namespace hotkeys {

    namespace {

        enum class Delivery {
            // retain one edge until the owner consumes it
            Queued,
            // return the edge to the sampler loop for worker-owned dispatch
            Worker,
            // publish held state without retaining or dispatching the edge
            StateOnly,
        };

        constexpr auto SAMPLE_INTERVAL = std::chrono::milliseconds(8);
        constexpr size_t ACTION_COUNT = static_cast<size_t>(Action::Count);
        constexpr size_t CARD_UNIT_COUNT = 2;
        constexpr size_t OVERLAY_COUNT = games::OverlayButtons::Size;
        static_assert(static_cast<size_t>(Action::InsertCardP1) == 0);
        static_assert(static_cast<size_t>(Action::InsertCardP2) == 1);

        // pending is saturating: one unhandled edge is retained without replaying input
        std::array<std::atomic_bool, ACTION_COUNT> ACTION_PENDING {};
        std::array<std::atomic_bool, ACTION_COUNT> ACTION_PRESSED {};
        std::array<std::atomic_bool, OVERLAY_COUNT> OVERLAY_PENDING {};
        std::atomic_bool HOTKEY_TOGGLE_ACTIVE {false};
        std::mutex INPUT_MUTEX;
        std::mutex DISPATCH_MUTEX;
        bool INPUT_ENABLED = false;
        bool DISPATCH_ENABLED = false;
        bool RAW_INPUT_ENABLED = false;
        bool HOTKEY_GATE_ENABLED = false;
        std::jthread WORKER;

        bool read_button(std::vector<Button> *buttons, size_t index) {
            // getState retains each binding's focus, modifier, inversion, and debounce policy
            return RI_MGR && buttons && index < buttons->size() &&
                GameAPI::Buttons::getState(RI_MGR, buttons->at(index));
        }

        Delivery overlay_delivery(size_t index) {
            // these actions must not depend on a render-thread consumer
            switch (index) {
                case games::OverlayButtons::InsertCoin:
                case games::OverlayButtons::SuperExit:
                case games::OverlayButtons::HotkeyToggle:
                    return Delivery::Worker;
                case games::OverlayButtons::HotkeyEnable1:
                case games::OverlayButtons::HotkeyEnable2:
                    return Delivery::StateOnly;
                default:
                    return Delivery::Queued;
            }
        }

        bool overlay_requires_gate(size_t index) {
            // coin and the gate controls retain their legacy ungated behavior
            return index != games::OverlayButtons::InsertCoin &&
                index != games::OverlayButtons::HotkeyEnable1 &&
                index != games::OverlayButtons::HotkeyEnable2 &&
                index != games::OverlayButtons::HotkeyToggle;
        }

        bool overlay_requires_owner(size_t index) {
            switch (index) {
                case games::OverlayButtons::ToggleAllWindows:
                case games::OverlayButtons::ToggleMainMenu:
                case games::OverlayButtons::ToggleSubScreen:
                case games::OverlayButtons::ToggleIOPanel:
                case games::OverlayButtons::ToggleConfig:
                case games::OverlayButtons::ToggleVirtualKeypadP1:
                case games::OverlayButtons::ToggleVirtualKeypadP2:
                case games::OverlayButtons::ToggleCardManager:
                case games::OverlayButtons::ToggleLog:
                case games::OverlayButtons::ToggleControl:
                case games::OverlayButtons::TogglePatchManager:
                case games::OverlayButtons::ToggleScreenResize:
                case games::OverlayButtons::ToggleFps:
                case games::OverlayButtons::ToggleCameraControl:
                case games::OverlayButtons::ToggleOBSControl:
                case games::OverlayButtons::ScreenResize:
                case games::OverlayButtons::ScreenResizeScene1:
                case games::OverlayButtons::ScreenResizeScene2:
                case games::OverlayButtons::ScreenResizeScene3:
                case games::OverlayButtons::ScreenResizeScene4:
                    return true;
                default:
                    return false;
            }
        }

        bool overlay_action_enabled(size_t index) {
            if (index == games::OverlayButtons::TriggerPinMacroP1) {
                return eamuse_pin_macro_is_active(0);
            }
            if (index == games::OverlayButtons::TriggerPinMacroP2) {
                return eamuse_pin_macro_is_active(1);
            }
            return true;
        }

        bool read_alt_f4(bool raw_input_enabled) {
            // preserve both legacy detection paths: Win32 state and every raw keyboard page
            bool pressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 &&
                (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            if (!raw_input_enabled || !RI_MGR) {
                return pressed;
            }
            return pressed || RI_MGR->keyboard_combo_pressed(VK_MENU, VK_F4);
        }

        template <size_t Size>
        bool record_sample(
                size_t index,
                bool current,
                std::array<bool, Size> &previous,
                std::array<std::atomic_bool, Size> &pending,
                Delivery delivery) {
            const bool rising_edge = current && !previous[index];
            if (rising_edge && delivery == Delivery::Queued) {
                pending[index].store(true, std::memory_order_relaxed);
            }
            previous[index] = current;
            return rising_edge;
        }

        template <size_t Size>
        void clear_state(
                std::array<std::atomic_bool, Size> &pending,
                std::array<std::atomic_bool, Size> &pressed) {
            for (auto &value : pending) {
                value.store(false, std::memory_order_relaxed);
            }
            for (auto &value : pressed) {
                value.store(false, std::memory_order_relaxed);
            }
        }

        void run(std::stop_token stop_token) {
            std::array<bool, OVERLAY_COUNT> overlay_previous {};
            std::array<bool, ACTION_COUNT> action_previous {};
            bool alt_f4_previous = false;

            while (!stop_token.stop_requested()) {
                bool coin_edge = false;
                bool super_exit_edge = false;
                bool input_enabled = false;
                bool dispatch_input = false;
                bool alt_f4_current = false;

                {
                    // disable_input holds this mutex until any raw-input poll has completed
                    std::lock_guard<std::mutex> lock(INPUT_MUTEX);
                    input_enabled = INPUT_ENABLED;

                    if (input_enabled) {
                        // game selection is fixed in non-standalone mode, while rebinding
                        // mutates these existing Button objects in place
                        auto *overlay_buttons = games::get_buttons_overlay(eamuse_get_game());
                        auto *keypad_buttons = games::get_buttons_keypads(eamuse_get_game());

                        const auto enable1 = games::OverlayButtons::HotkeyEnable1;
                        const auto enable2 = games::OverlayButtons::HotkeyEnable2;
                        const auto toggle = games::OverlayButtons::HotkeyToggle;
                        const bool enable1_pressed = read_button(overlay_buttons, enable1);
                        const bool enable2_pressed = read_button(overlay_buttons, enable2);

                        record_sample(
                                enable1,
                                enable1_pressed,
                                overlay_previous,
                                OVERLAY_PENDING,
                                Delivery::StateOnly);
                        record_sample(
                                enable2,
                                enable2_pressed,
                                overlay_previous,
                                OVERLAY_PENDING,
                                Delivery::StateOnly);

                        if (record_sample(
                                toggle,
                            HOTKEY_GATE_ENABLED && read_button(overlay_buttons, toggle),
                                overlay_previous,
                                OVERLAY_PENDING,
                                Delivery::Worker)) {
                            const bool active = HOTKEY_TOGGLE_ACTIVE.load(std::memory_order_relaxed);
                            HOTKEY_TOGGLE_ACTIVE.store(!active, std::memory_order_relaxed);
                        }

                        const bool enable1_set = overlay_buttons &&
                            enable1 < overlay_buttons->size() && overlay_buttons->at(enable1).isSet();
                        const bool enable2_set = overlay_buttons &&
                            enable2 < overlay_buttons->size() && overlay_buttons->at(enable2).isSet();
                        const bool hotkey_gate_active =
                            !HOTKEY_GATE_ENABLED ||
                            HOTKEY_TOGGLE_ACTIVE.load(std::memory_order_relaxed) ||
                            ((!enable1_set || enable1_pressed) &&
                             (!enable2_set || enable2_pressed));

                        for (size_t index = 0; index < OVERLAY_COUNT; index++) {
                            if (index == enable1 || index == enable2 || index == toggle) {
                                continue;
                            }

                            bool current = overlay_action_enabled(index) &&
                                read_button(overlay_buttons, index);
                            if (overlay_requires_owner(index) && !HOTKEY_GATE_ENABLED) {
                                current = false;
                            }
                            if (overlay_requires_gate(index)) {
                                current = current && hotkey_gate_active;
                            }

                            const bool rising_edge = record_sample(
                                    index,
                                    current,
                                    overlay_previous,
                                    OVERLAY_PENDING,
                                    overlay_delivery(index));
                            if (index == games::OverlayButtons::InsertCoin) {
                                coin_edge = rising_edge;
                            } else if (index == games::OverlayButtons::SuperExit) {
                                super_exit_edge = rising_edge;
                            }
                        }

                        for (size_t unit = 0; unit < CARD_UNIT_COUNT; unit++) {
                            const auto index = games::KeypadButtons::InsertCard +
                                unit * games::KeypadButtons::Size;
                                const bool current = read_button(keypad_buttons, index);
                                record_sample(
                                    unit,
                                    current,
                                    action_previous,
                                    ACTION_PENDING,
                                    Delivery::Queued);
                                ACTION_PRESSED[unit].store(current, std::memory_order_relaxed);
                        }

                        dispatch_input = true;
                    } else {
                        overlay_previous.fill(false);
                        action_previous.fill(false);
                    }

                    alt_f4_current = read_alt_f4(RAW_INPUT_ENABLED);
                }

                if (dispatch_input) {
                    // disable_input uses this barrier to wait for callbacks already in flight
                    std::lock_guard<std::mutex> dispatch_lock(DISPATCH_MUTEX);
                    if (DISPATCH_ENABLED) {
                        if (coin_edge) {
                            eamuse_coin_insert();
                        }
                        eamuse_pin_macro_tick();
                    }
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

        clear_state(ACTION_PENDING, ACTION_PRESSED);
        for (auto &pending : OVERLAY_PENDING) {
            pending.store(false, std::memory_order_relaxed);
        }
        HOTKEY_TOGGLE_ACTIVE.store(false, std::memory_order_relaxed);

        WORKER = std::jthread(run);
        log_info("hotkeys", "sampler started");
    }

    void enable_raw_input() {
        std::lock_guard<std::mutex> input_lock(INPUT_MUTEX);
        RAW_INPUT_ENABLED = true;
    }

    void enable_input(bool hotkey_gate_enabled) {
        {
            std::lock_guard<std::mutex> dispatch_lock(DISPATCH_MUTEX);
            DISPATCH_ENABLED = true;
        }
        std::lock_guard<std::mutex> input_lock(INPUT_MUTEX);
        clear_state(ACTION_PENDING, ACTION_PRESSED);
        for (auto &pending : OVERLAY_PENDING) {
            pending.store(false, std::memory_order_relaxed);
        }
        HOTKEY_TOGGLE_ACTIVE.store(false, std::memory_order_relaxed);
        HOTKEY_GATE_ENABLED = hotkey_gate_enabled;
        INPUT_ENABLED = true;
        log_info("hotkeys", "configured input enabled");
    }

    void disable_input() {
        {
            std::lock_guard<std::mutex> input_lock(INPUT_MUTEX);
            INPUT_ENABLED = false;
            clear_state(ACTION_PENDING, ACTION_PRESSED);
            for (auto &pending : OVERLAY_PENDING) {
                pending.store(false, std::memory_order_relaxed);
            }
            HOTKEY_TOGGLE_ACTIVE.store(false, std::memory_order_relaxed);
        }
        {
            std::lock_guard<std::mutex> dispatch_lock(DISPATCH_MUTEX);
            DISPATCH_ENABLED = false;
        }
        log_info("hotkeys", "configured input disabled");
    }

    void disable_raw_input() {
        std::lock_guard<std::mutex> input_lock(INPUT_MUTEX);
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

    bool consume(Action action) {
        const auto index = static_cast<size_t>(action);
        return index < ACTION_COUNT &&
            ACTION_PENDING[index].exchange(false, std::memory_order_relaxed);
    }

    bool consume_overlay_button(size_t index) {
        return index < OVERLAY_COUNT &&
            overlay_delivery(index) == Delivery::Queued &&
            OVERLAY_PENDING[index].exchange(false, std::memory_order_relaxed);
    }

    void enable_hotkey_gate() {
        std::lock_guard<std::mutex> input_lock(INPUT_MUTEX);
        HOTKEY_GATE_ENABLED = true;
    }

    void disable_hotkey_gate() {
        std::lock_guard<std::mutex> input_lock(INPUT_MUTEX);
        HOTKEY_GATE_ENABLED = false;
        HOTKEY_TOGGLE_ACTIVE.store(false, std::memory_order_relaxed);
        for (size_t index = 0; index < OVERLAY_COUNT; index++) {
            if (overlay_requires_owner(index)) {
                OVERLAY_PENDING[index].store(false, std::memory_order_relaxed);
            }
        }
    }

    bool pressed(Action action) {
        const auto index = static_cast<size_t>(action);
        return index < ACTION_COUNT && ACTION_PRESSED[index].load(std::memory_order_relaxed);
    }

}