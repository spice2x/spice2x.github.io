#include "poke.h"

#include <thread>
#include <optional>

#include "games/nost/io.h"
#include "cfg/api.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "touch/native/inject.h"
#include "util/logging.h"
#include "util/precise_timer.h"

namespace games::nost::poke {

    static std::thread *THREAD = nullptr;
    static volatile bool THREAD_RUNNING = false;
    
    static const std::unordered_map<int, std::pair<LONG, LONG>> NOST_POKE_NUM {
        {EAM_IO_KEYPAD_0, {760, 440}},
        {EAM_IO_KEYPAD_1, {760, 385}},
        {EAM_IO_KEYPAD_2, {820, 385}},
        {EAM_IO_KEYPAD_3, {880, 385}},
        {EAM_IO_KEYPAD_4, {760, 325}},
        {EAM_IO_KEYPAD_5, {820, 325}},
        {EAM_IO_KEYPAD_6, {880, 325}},
        {EAM_IO_KEYPAD_7, {760, 265}},
        {EAM_IO_KEYPAD_8, {820, 265}},
        {EAM_IO_KEYPAD_9, {880, 265}},
        {EAM_IO_KEYPAD_00, {820, 440}}, // erase button
        {EAM_IO_KEYPAD_DECIMAL, {880, 440}} // also erase button
    };

    static const std::unordered_map<games::nost::Buttons::Button, std::pair<LONG, LONG>> NOST_POKE {
        {games::nost::Buttons::Button::PokeConfirm, {1100, 525}},
        {games::nost::Buttons::Button::PokeBack, {340, 100}},
        {games::nost::Buttons::Button::PokeSong1, {450, 190}},
        {games::nost::Buttons::Button::PokeSong2, {450, 330}},
        {games::nost::Buttons::Button::PokeSong3, {450, 480}},
        {games::nost::Buttons::Button::PokeSong4, {840, 190}},
        {games::nost::Buttons::Button::PokeSong5, {840, 330}},
        {games::nost::Buttons::Button::PokeSong6, {840, 480}},
        {games::nost::Buttons::Button::PokeDifficulty1, {820, 240}},
        {games::nost::Buttons::Button::PokeDifficulty2, {820, 320}},
        {games::nost::Buttons::Button::PokeDifficulty3, {820, 410}},
        {games::nost::Buttons::Button::PokeDifficulty4, {820, 490}},
    };

    static bool inject_poke_position(POINT position, bool down) {
        return nativetouch::inject::inject_synthetic_touch(position, down);
    }

    void enable() {
        // check if already running
        if (THREAD) {
            return;
        }

        // create new thread
        THREAD_RUNNING = true;
        THREAD = new std::thread([] {
            timeutils::PreciseSleepTimer timer;

            const int swipe_anim_total_frames = 6;
            const int swipe_anim_y = 300;

            const int swipe_next_page_x_begin = 1120;
            const int swipe_next_page_x_end = 1120 - 120;

            const int swipe_prev_page_x_begin = 280 - 120;
            const int swipe_prev_page_x_end = 280;

            int next_page_anim_index = -1;
            int prev_page_anim_index = -1;
            bool contact_active = false;
            bool release_pending = false;
            POINT last_position {};
            std::unordered_map<games::nost::Buttons::Button, bool> button_states;
            uint16_t last_keypad_state = 0;

            // log
            log_info("poke", "enabled");

            // set variable to false to stop
            while (THREAD_RUNNING) {
                if (release_pending) {
                    if (contact_active && !inject_poke_position(last_position, false)) {
                        timer.sleep(30);
                        continue;
                    }
                    contact_active = false;
                    release_pending = false;
                }

                auto &buttons = games::nost::get_buttons();
                std::optional<POINT> touch_position;
                bool release_after_touch = false;

                if (0 <= next_page_anim_index) {
                    const auto delta =
                        (swipe_next_page_x_end - swipe_next_page_x_begin)
                        * (swipe_anim_total_frames - next_page_anim_index) / swipe_anim_total_frames;

                    touch_position = POINT {
                        swipe_next_page_x_begin + delta,
                        swipe_anim_y,
                    };
                    next_page_anim_index--;
                    if (next_page_anim_index < 0) {
                        release_after_touch = true;
                    }
                } else if (0 <= prev_page_anim_index) {
                    const auto delta =
                        (swipe_prev_page_x_end - swipe_prev_page_x_begin)
                        * (swipe_anim_total_frames - prev_page_anim_index) / swipe_anim_total_frames;

                    touch_position = POINT {
                        swipe_prev_page_x_begin + delta,
                        swipe_anim_y,
                    };
                    prev_page_anim_index--;
                    if (prev_page_anim_index < 0) {
                        release_after_touch = true;
                    }
                } else {
                    const auto button_triggered = [&](games::nost::Buttons::Button button) {
                        const auto pressed =
                            GameAPI::Buttons::getState(RI_MGR, buttons.at(button));
                        const auto triggered = pressed && !button_states[button];
                        button_states[button] = pressed;
                        return triggered;
                    };

                    for (const auto& it : NOST_POKE) {
                        if (button_triggered(it.first) && !touch_position.has_value()) {
                            touch_position = POINT {
                                it.second.first,
                                it.second.second,
                            };
                            release_after_touch = true;
                        }
                    }

                    const auto next_triggered =
                        button_triggered(games::nost::Buttons::PokeNextPage);
                    const auto prev_triggered =
                        button_triggered(games::nost::Buttons::PokePrevPage);

                    const auto state = eamuse_get_keypad_state(0);
                    if (!touch_position.has_value()) {
                        if (state) {
                            for (const auto& it : NOST_POKE_NUM) {
                                const auto mask = static_cast<uint16_t>(1 << it.first);
                                if ((state & mask) && !(last_keypad_state & mask)) {
                                    touch_position = POINT {
                                        it.second.first,
                                        it.second.second,
                                    };
                                    release_after_touch = true;
                                    break;
                                }
                            }
                        }
                    }
                    last_keypad_state = state;

                    // start animations for next frame
                    if (!touch_position.has_value()) {
                        if (next_triggered) {
                            next_page_anim_index = swipe_anim_total_frames;
                        }

                        if (prev_triggered) {
                            prev_page_anim_index = swipe_anim_total_frames;
                        }
                    }
                }

                if (touch_position.has_value() &&
                    inject_poke_position(*touch_position, true)) {
                    contact_active = true;
                    last_position = *touch_position;
                }
                if (release_after_touch && contact_active) {
                    release_pending = true;
                }

                // slow down
                timer.sleep(30);
            }

            if (contact_active) {
                inject_poke_position(last_position, false);
            }

            return nullptr;
        });
    }

    void disable() {
        if (!THREAD) {
            return;
        }

        // stop old thread
        THREAD_RUNNING = false;
        THREAD->join();

        // delete thread
        delete THREAD;
        THREAD = nullptr;

        // log
        log_info("poke", "disabled");
    }
}
