#include "poke.h"

#include <thread>

#include "windows.h"
#include "games/io.h"
#include "hooks/graphics/graphics.h"
#include "launcher/shutdown.h"
#include "misc/eamuse.h"
#include "touch/native/inject.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/precise_timer.h"

namespace games::iidx::poke {

    static std::thread *THREAD = nullptr;
    static bool THREAD_RUNNING = false;

    struct KeypadMapping {
        char character;
        uint16_t state;
    };

    static KeypadMapping KEYPAD_MAPPINGS[] = {
        { '0', 1 << EAM_IO_KEYPAD_0 },
        { '1', 1 << EAM_IO_KEYPAD_1 },
        { '2', 1 << EAM_IO_KEYPAD_2 },
        { '3', 1 << EAM_IO_KEYPAD_3 },
        { '4', 1 << EAM_IO_KEYPAD_4 },
        { '5', 1 << EAM_IO_KEYPAD_5 },
        { '6', 1 << EAM_IO_KEYPAD_6 },
        { '7', 1 << EAM_IO_KEYPAD_7 },
        { '8', 1 << EAM_IO_KEYPAD_8 },
        { '9', 1 << EAM_IO_KEYPAD_9 },
        { 'A', 1 << EAM_IO_KEYPAD_00 },

        // Touch panel keypad does not have the decimal key.
        // This will be used for toggling the keypad instead
        { 'D', 1 << EAM_IO_KEYPAD_DECIMAL },
    };

    const int IIDX_KEYPAD_BUTTON_SIZE = 90;
    const int IIDX_KEYPAD_GAP = 15;
    const int IIDX_KEYPAD_TOP = 570;
    const int IIDX_KEYPAD_LEFT_1P = 90;
    const int IIDX_KEYPAD_LEFT_2P = 1530;

    // <KeypadID>/<Key> mapped to 1080p coordinate space
    static const std::unordered_map<std::string, int> IIDX_KEYPAD_POSITION_X {
        {"0/0", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/1", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/2", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"0/3", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 2.5 + IIDX_KEYPAD_GAP * 2)},
        {"0/4", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/5", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"0/6", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 2.5 + IIDX_KEYPAD_GAP * 2)},
        {"0/7", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/8", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"0/9", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 2.5 + IIDX_KEYPAD_GAP * 2)},
        {"0/A", (int)(IIDX_KEYPAD_LEFT_1P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"0/D", 60},
        {"1/0", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/1", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/2", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"1/3", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 2.5 + IIDX_KEYPAD_GAP * 2)},
        {"1/4", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/5", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"1/6", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 2.5 + IIDX_KEYPAD_GAP * 2)},
        {"1/7", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/8", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"1/9", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 2.5 + IIDX_KEYPAD_GAP * 2)},
        {"1/A", (int)(IIDX_KEYPAD_LEFT_2P + IIDX_KEYPAD_BUTTON_SIZE * 1.5 + IIDX_KEYPAD_GAP)},
        {"1/D", 1920 - 60},
    };

    static const std::unordered_map<std::string, int> IIDX_KEYPAD_POSITION_Y {
        {"0/0", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 3 + IIDX_KEYPAD_BUTTON_SIZE * 3.5)},
        {"0/1", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 2 + IIDX_KEYPAD_BUTTON_SIZE * 2.5)},
        {"0/2", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 2 + IIDX_KEYPAD_BUTTON_SIZE * 2.5)},
        {"0/3", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 2 + IIDX_KEYPAD_BUTTON_SIZE * 2.5)},
        {"0/4", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP + IIDX_KEYPAD_BUTTON_SIZE * 1.5)},
        {"0/5", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP + IIDX_KEYPAD_BUTTON_SIZE * 1.5)},
        {"0/6", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP + IIDX_KEYPAD_BUTTON_SIZE * 1.5)},
        {"0/7", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/8", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/9", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"0/A", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 3 + IIDX_KEYPAD_BUTTON_SIZE * 3.5)},
        {"0/D", 50},
        {"1/0", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 3 + IIDX_KEYPAD_BUTTON_SIZE * 3.5)},
        {"1/1", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 2 + IIDX_KEYPAD_BUTTON_SIZE * 2.5)},
        {"1/2", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 2 + IIDX_KEYPAD_BUTTON_SIZE * 2.5)},
        {"1/3", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 2 + IIDX_KEYPAD_BUTTON_SIZE * 2.5)},
        {"1/4", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP + IIDX_KEYPAD_BUTTON_SIZE * 1.5)},
        {"1/5", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP + IIDX_KEYPAD_BUTTON_SIZE * 1.5)},
        {"1/6", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP + IIDX_KEYPAD_BUTTON_SIZE * 1.5)},
        {"1/7", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/8", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/9", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_BUTTON_SIZE * 0.5)},
        {"1/A", (int)(IIDX_KEYPAD_TOP + IIDX_KEYPAD_GAP * 3 + IIDX_KEYPAD_BUTTON_SIZE * 3.5)},
        {"1/D", 50},
    };

    static void inject_native_touch_points(const std::vector<TouchPoint> &touch_points, bool down) {
        if (touch_points.empty()) {
            return;
        }

        const auto &touch = touch_points.front();
        nativetouch::inject::inject_synthetic_touch({ touch.x, touch.y }, down);
    }
    
    void enable() {

        // check if already running
        if (THREAD)
            return;

        // create new thread
        THREAD_RUNNING = true;
        THREAD = new std::thread([] {
            timeutils::PreciseSleepTimer timer;

            // log
            log_info("poke", "enabled");


            std::vector<TouchPoint> touch_points;
            std::vector<uint16_t> last_keypad_state = {0, 0};

            // set variable to false to stop
            while (THREAD_RUNNING) {

                // clean up touch from last frame
                if (touch_points.size() > 0) {
                    inject_native_touch_points(touch_points, false);
                    touch_points.clear();
                }

                for (int unit = 0; unit < 2; unit++) {
                    // get keypad state
                    auto state = eamuse_get_keypad_state(unit);

                    if (state != 0) {
                        // add keys
                        for (auto &mapping : KEYPAD_MAPPINGS) {
                            if (state & mapping.state) {
                                if (last_keypad_state[unit] & mapping.state) {
                                    // log_warning("poke", "ignoring hold {} {}", unit, mapping.character);
                                    continue;
                                }
                                std::string handle = fmt::format("{0}/{1}", unit, mapping.character);

                                auto x_iter = IIDX_KEYPAD_POSITION_X.find(handle);
                                auto y_iter = IIDX_KEYPAD_POSITION_Y.find(handle);

                                if (x_iter != IIDX_KEYPAD_POSITION_X.end() && y_iter != IIDX_KEYPAD_POSITION_Y.end()) {
                                    DWORD touch_id = (DWORD)(0xFFFFFF * unit + mapping.character);

                                    float x = x_iter->second / 1920.0;
                                    float y = y_iter->second / 1080.0;
                                    if (GRAPHICS_WINDOWED) {
                                        if (SPICETOUCH_TOUCH_WIDTH <= 0 || SPICETOUCH_TOUCH_HEIGHT <= 0) {
                                            continue;
                                        }
                                        x = SPICETOUCH_TOUCH_X + x * SPICETOUCH_TOUCH_WIDTH;
                                        y = SPICETOUCH_TOUCH_Y + y * SPICETOUCH_TOUCH_HEIGHT;
                                    } else {
                                        x = x_iter->second;
                                        y = y_iter->second;
                                    }

                                    TouchPoint tp {
                                        .id = touch_id,
                                        .x = (LONG)x,
                                        .y = (LONG)y,
                                        .mouse = true,
                                    };
                                    touch_points.emplace_back(tp);
                                    // log_warning("poke", "coords: {} {}", to_string(tp.x), to_string(tp.y));
                                }
                            }
                        } // for all keys
                    } // if state != 0

                    last_keypad_state[unit] = state;

                } // for all units

                if (touch_points.size() > 0) {
                    inject_native_touch_points(touch_points, true);
                }

                // slow down
                timer.sleep(50);
            }

            if (!touch_points.empty()) {
                inject_native_touch_points(touch_points, false);
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
