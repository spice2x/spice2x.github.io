#include "poke.h"

#include <thread>

#include "windows.h"
#include "cfg/screen_resize.h"
#include "games/io.h"
#include "games/iidx/iidx.h"
#include "hooks/graphics/graphics.h"
#include "launcher/shutdown.h"
#include "misc/eamuse.h"
#include "overlay/overlay.h"
#include "overlay/windows/generic_sub.h"
#include "rawinput/rawinput.h"
#include "touch/touch.h"
#include "util/libutils.h"
#include "util/logging.h"

#define POKE_NATIVE_TOUCH 0

#if POKE_NATIVE_TOUCH
static HINSTANCE USER32_INSTANCE = nullptr;
typedef BOOL (WINAPI *InitializeTouchInjection_t)(UINT32, DWORD);
typedef BOOL (WINAPI *InjectTouchInput_t)(UINT32, POINTER_TOUCH_INFO*);
static InitializeTouchInjection_t pInitializeTouchInjection = nullptr;
static InjectTouchInput_t pInjectTouchInput = nullptr;
#endif

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

#if POKE_NATIVE_TOUCH
    void initialize_native_touch_library() {
        if (USER32_INSTANCE == nullptr) {
            USER32_INSTANCE = libutils::load_library("user32.dll");
        }

        pInitializeTouchInjection = libutils::try_proc<InitializeTouchInjection_t>(
                USER32_INSTANCE, "InitializeTouchInjection");
        pInjectTouchInput = libutils::try_proc<InjectTouchInput_t>(
                USER32_INSTANCE, "InjectTouchInput");
    }

    void emulate_native_touch(TouchPoint tp, bool is_down) {
        if (pInjectTouchInput == nullptr) {
            return;
        }

        POINTER_TOUCH_INFO contact;
        BOOL bRet = TRUE;
        const int contact_offset = 2;

        memset(&contact, 0, sizeof(POINTER_TOUCH_INFO));

        contact.pointerInfo.pointerType = PT_TOUCH;
        contact.pointerInfo.pointerId = 0;
        contact.pointerInfo.ptPixelLocation.x = tp.x;
        contact.pointerInfo.ptPixelLocation.y = tp.y;
        if (is_down) {
            contact.pointerInfo.pointerFlags = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
        } else {
            contact.pointerInfo.pointerFlags = POINTER_FLAG_UP;
        }

        contact.pointerInfo.dwTime = 0;
        contact.pointerInfo.PerformanceCount = 0;

        contact.touchFlags = TOUCH_FLAG_NONE;
        contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
        contact.orientation = 0;
        contact.pressure = 1024;

        contact.rcContact.top = tp.y - contact_offset;
        contact.rcContact.bottom = tp.y + contact_offset;
        contact.rcContact.left = tp.x - contact_offset;
        contact.rcContact.right = tp.x + contact_offset;

        bRet = InjectTouchInput(1, &contact);
        if (!bRet) {
            log_warning("poke", "error injecting native touch {}: ({}, {}) error: {}", is_down ? "down" : "up", tp.x, tp.y, GetLastError());
        }
    }

    void emulate_native_touch_points(std::vector<TouchPoint> *touch_points) {
        int i = 0;
        for (auto &touch : *touch_points) {
            emulate_native_touch(touch, true);
        }
    }

    void clear_native_touch_points(std::vector<TouchPoint> *touch_points) {
        for (auto &touch : *touch_points) {
            emulate_native_touch(touch, false);
        }
        touch_points->clear();
    }
#endif
    
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
        if (THREAD)
            return;

        // create new thread
        THREAD_RUNNING = true;
        THREAD = new std::thread([] {

            // log
            log_info("poke", "enabled");


            std::vector<TouchPoint> touch_points;
            std::vector<uint16_t> last_keypad_state = {0, 0};

#if POKE_NATIVE_TOUCH
            bool use_native = games::iidx::NATIVE_TOUCH;

            if (use_native) {
                initialize_native_touch_library();
                
                if (pInitializeTouchInjection != nullptr) {
                    pInitializeTouchInjection(1, TOUCH_FEEDBACK_NONE);
                }
            }

#else 
            bool use_native = false;
#endif

            // set variable to false to stop
            while (THREAD_RUNNING) {

                // clean up touch from last frame
                if (touch_points.size() > 0) {
#if POKE_NATIVE_TOUCH
                    if (use_native) {
                        clear_native_touch_points(&touch_points);
                    } else {
                        clear_touch_points(&touch_points);
                    }
#else
                    clear_touch_points(&touch_points);
#endif
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
                                    if (use_native) {
                                        x *= rawinput::TOUCHSCREEN_RANGE_X;
                                        y *= rawinput::TOUCHSCREEN_RANGE_Y;
                                    } else if (GRAPHICS_IIDX_WSUB) {
                                        // Scale to windowed subscreen
                                        x *= GRAPHICS_IIDX_WSUB_WIDTH;
                                        y *= GRAPHICS_IIDX_WSUB_HEIGHT;
                                    } else if (GENERIC_SUB_WINDOW_FULLSIZE || !overlay::OVERLAY->get_active()) {
                                        // Overlay is not present, scale to main screen
                                        if (GRAPHICS_WINDOWED) {
                                            x *= SPICETOUCH_TOUCH_WIDTH;
                                            y *= SPICETOUCH_TOUCH_HEIGHT;
                                        } else {
                                            x *= ImGui::GetIO().DisplaySize.x;
                                            y *= ImGui::GetIO().DisplaySize.y;
                                        }
                                    } else {
                                        // Overlay subscreen
                                        x = (GENERIC_SUB_WINDOW_X + x * GENERIC_SUB_WINDOW_WIDTH);
                                        y = (GENERIC_SUB_WINDOW_Y + y * GENERIC_SUB_WINDOW_HEIGHT);

                                        // Scale to window size ratio
                                        if (GRAPHICS_WINDOWED) {
                                            x *= SPICETOUCH_TOUCH_WIDTH / ImGui::GetIO().DisplaySize.x;
                                            y *= SPICETOUCH_TOUCH_HEIGHT / ImGui::GetIO().DisplaySize.y;
                                        }
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
#if POKE_NATIVE_TOUCH
                    if (use_native) {
                        emulate_native_touch_points(&touch_points);
                    } else {
                        touch_write_points(&touch_points);
                    }
#else
                    touch_write_points(&touch_points);
#endif
                }

                // slow down
                Sleep(50);
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
