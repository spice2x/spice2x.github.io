#include "poke.h"

#include <thread>
#include <optional>

#include "games/nost/io.h"
#include "cfg/api.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "touch/touch.h"
#include "util/logging.h"

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

    void clear_touch_points(std::vector<TouchPoint> *touch_points) {
        std::vector<DWORD> touch_ids;
        for (auto &touch : *touch_points) {
            touch_ids.emplace_back(touch.id);
        }
        touch_remove_points(&touch_ids);
        touch_points->clear();
    }

    void enable() {
        // check if already running
        if (THREAD) {
            return;
        }

        // create new thread
        THREAD_RUNNING = true;
        THREAD = new std::thread([] {
            const DWORD touch_id = (DWORD)(0xFFFFFFFE);

            const int swipe_anim_total_frames = 6;
            const int swipe_anim_y = 300;

            const int swipe_next_page_x_begin = 1120;
            const int swipe_next_page_x_end = 1120-120;

            const int swipe_prev_page_x_begin = 280-120;
            const int swipe_prev_page_x_end = 280;

            int next_page_anim_index = -1;
            int prev_page_anim_index = -1;
            std::vector<TouchPoint> touch_points;

            bool touch_release = false;

            // log
            log_info("poke", "enabled");

            // set variable to false to stop
            while (THREAD_RUNNING) {
                // clean up touch from last frame
                if (!touch_points.empty()) {
                    if (touch_release) {
                        clear_touch_points(&touch_points);
                        touch_release = false;
                    } else {
                        touch_points.clear();
                    }
                }

                auto &buttons = games::nost::get_buttons();

                if (0 <= next_page_anim_index) {
                    const auto delta =
                        (swipe_next_page_x_end - swipe_next_page_x_begin)
                        * (swipe_anim_total_frames - next_page_anim_index) / swipe_anim_total_frames;

                    TouchPoint tp {
                        .id = touch_id,
                        .x = (LONG)swipe_next_page_x_begin + delta,
                        .y = (LONG)swipe_anim_y,
                        .mouse = true,
                    };
                    touch_points.emplace_back(tp);
                    next_page_anim_index--;
                    if (next_page_anim_index < 0) {
                        touch_release = true;
                    }
                } else if (0 <= prev_page_anim_index) {
                    const auto delta =
                        (swipe_prev_page_x_end - swipe_prev_page_x_begin)
                        * (swipe_anim_total_frames - prev_page_anim_index) / swipe_anim_total_frames;

                    TouchPoint tp {
                        .id = touch_id,
                        .x = (LONG)swipe_prev_page_x_begin + delta,
                        .y = (LONG)swipe_anim_y,
                        .mouse = true,
                    };
                    touch_points.emplace_back(tp);
                    prev_page_anim_index--;
                    if (prev_page_anim_index < 0) {
                        touch_release = true;
                    }
                } else {
                    for (const auto& it : NOST_POKE) {
                        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(it.first))) {
                            TouchPoint tp {
                                .id = touch_id,
                                .x = it.second.first,
                                .y = it.second.second,
                                .mouse = true,
                            };
                            touch_points.emplace_back(tp);
                            touch_release = true;
                            break;
                        }
                    }
                    if (!touch_release) {
                        const auto state = eamuse_get_keypad_state(0);
                        if (state) {
                            for (const auto& it : NOST_POKE_NUM) {
                                if (state & (1 << it.first)) {
                                    TouchPoint tp {
                                        .id = touch_id,
                                        .x = it.second.first,
                                        .y = it.second.second,
                                        .mouse = true,
                                    };
                                    touch_points.emplace_back(tp);
                                    touch_release = true;
                                    break;
                                }
                            }
                        }
                    }

                    // start animations for next frame
                    if (!touch_release) {
                        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(games::nost::Buttons::PokeNextPage))) {
                            next_page_anim_index = swipe_anim_total_frames;
                        }
                        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(games::nost::Buttons::PokePrevPage))) {
                            prev_page_anim_index = swipe_anim_total_frames;
                        }
                    }
                }

                if (!touch_points.empty()) {
                    touch_write_points(&touch_points);
                }

                // slow down
                Sleep(30);
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
