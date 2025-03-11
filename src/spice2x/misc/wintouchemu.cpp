// enable touch functions - set version to windows 7
// mingw otherwise doesn't load touch stuff
#define _WIN32_WINNT 0x0601

#include "wintouchemu.h"

#include <chrono>
#include <algorithm>
#include <optional>
#include <thread>

#include "cfg/screen_resize.h"
#include "games/iidx/iidx.h"
#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "overlay/windows/generic_sub.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

#include "avs/game.h"

namespace wintouchemu {

    typedef struct {
        POINT pos;
        bool last_button_pressed;
        DWORD touch_event;
    } mouse_state_t;

    // settings
    bool FORCE = false;
    bool INJECT_MOUSE_AS_WM_TOUCH = false;
    bool LOG_FPS = false;

    static inline bool is_emu_enabled() {
        return FORCE || !is_touch_available() || GRAPHICS_SHOW_CURSOR;
    }

    static decltype(GetSystemMetrics) *GetSystemMetrics_orig = nullptr;
    static decltype(RegisterTouchWindow) *RegisterTouchWindow_orig = nullptr;

    static int WINAPI GetSystemMetrics_hook(int nIndex) {

        /*
         * fake touch screen
         * the game requires 0x01 and 0x02 flags to be set
         * 0x40 and 0x80 are set for completeness
         */
        if (nIndex == 94)
            return 0x01 | 0x02 | 0x40 | 0x80;

        // call original
        if (GetSystemMetrics_orig != nullptr) {
            return GetSystemMetrics_orig(nIndex);
        }

        // return error
        return 0;
    }

    static BOOL WINAPI RegisterTouchWindow_hook(HWND hwnd, ULONG ulFlags) {

        // don't register it if the emu is enabled
        if (is_emu_enabled()) {
            return true;
        }

        // call default
        return RegisterTouchWindow_orig(hwnd, ulFlags);
    }

    // state
    BOOL (WINAPI *GetTouchInputInfo_orig)(HANDLE, UINT, PTOUCHINPUT, int);
    bool USE_MOUSE = false;
    std::vector<TouchEvent> TOUCH_EVENTS;
    std::vector<TouchPoint> TOUCH_POINTS;
    HMODULE HOOKED_MODULE = nullptr;
    std::string WINDOW_TITLE_START = "";
    std::optional<std::string> WINDOW_TITLE_END = std::nullopt;
    volatile bool INITIALIZED = false;
    mouse_state_t mouse_state;

    void hook(const char *window_title, HMODULE module, int delay_in_s) {

        // hooks
        auto system_metrics_hook = detour::iat_try(
                "GetSystemMetrics", GetSystemMetrics_hook, module);
        auto register_touch_window_hook = detour::iat_try(
                "RegisterTouchWindow", RegisterTouchWindow_hook, module);

        // don't hook twice
        if (GetSystemMetrics_orig == nullptr) {
            GetSystemMetrics_orig = system_metrics_hook;
        }
        if (RegisterTouchWindow_orig == nullptr) {
            RegisterTouchWindow_orig = register_touch_window_hook;
        }

        // set module and title
        HOOKED_MODULE = module;
        WINDOW_TITLE_START = window_title;

        if (0 < delay_in_s) {
            // some games crash when touch events are injected too early during boot
            std::thread t([&]() {
                log_misc("wintouchemu", "defer initialization until later (with delay of {}s)", delay_in_s);
                std::this_thread::sleep_for(std::chrono::seconds(delay_in_s));
                log_misc("wintouchemu", "initializing", delay_in_s);
                INITIALIZED = true;
            });
            t.detach();
        } else {
            log_misc("wintouchemu", "initializing");
            INITIALIZED = true;
        }
    }

    void hook_title_ends(const char *window_title_start, const char *window_title_end, HMODULE module) {
        hook(window_title_start, module);

        WINDOW_TITLE_END = window_title_end;
    }

    static BOOL WINAPI GetTouchInputInfoHook(HANDLE hTouchInput, UINT cInputs, PTOUCHINPUT pInputs, int cbSize) {

        // check if original should be called
        if (hTouchInput != GetTouchInputInfoHook) {
            return GetTouchInputInfo_orig(hTouchInput, cInputs, pInputs, cbSize);
        }

        // set touch inputs
        bool result = false;
        bool mouse_used = false;
        for (UINT input = 0; input < cInputs; input++) {
            auto *touch_input = &pInputs[input];

            // clear touch input
            touch_input->x = 0;
            touch_input->y = 0;
            touch_input->hSource = nullptr;
            touch_input->dwID = 0;
            touch_input->dwFlags = 0;
            touch_input->dwMask = 0;
            touch_input->dwTime = 0;
            touch_input->dwExtraInfo = 0;
            touch_input->cxContact = 0;
            touch_input->cyContact = 0;

            // get touch event
            TouchEvent *touch_event = nullptr;
            if (TOUCH_EVENTS.size() > input) {
                touch_event = &TOUCH_EVENTS.at(input);
            }

            // check touch point
            if (touch_event) {
                // set touch point
                result = true;

                auto x = touch_event->x;
                auto y = touch_event->y;
                auto valid = true;

                // log_misc("wintouchemu", "touch event ({}, {})", to_string(x), to_string(y));

                if (GRAPHICS_IIDX_WSUB) {
                    // touch was received on subscreen window.
                    RECT clientRect {};
                    GetClientRect(TDJ_SUBSCREEN_WINDOW, &clientRect);
                    x = (float) x / clientRect.right * SPICETOUCH_TOUCH_WIDTH + SPICETOUCH_TOUCH_X;
                    y = (float) y / clientRect.bottom * SPICETOUCH_TOUCH_HEIGHT + SPICETOUCH_TOUCH_Y;
                } else if (overlay::OVERLAY) {
                    // touch was received on global coords
                    valid = overlay::OVERLAY->transform_touch_point(&x, &y);
                } else {
                    valid = false;
                }

                touch_input->x = x * 100;
                touch_input->y = y * 100;
                touch_input->hSource = hTouchInput;
                touch_input->dwID = touch_event->id;
                touch_input->dwFlags = 0;
                switch (touch_event->type) {
                    case TOUCH_DOWN:
                        if (valid) {
                            touch_input->dwFlags |= TOUCHEVENTF_DOWN;
                        }
                        break;
                    case TOUCH_MOVE:
                        if (valid) {
                            touch_input->dwFlags |= TOUCHEVENTF_MOVE;
                        }
                        break;
                    case TOUCH_UP:
                        // don't check valid so that this touch ID can be released
                        touch_input->dwFlags |= TOUCHEVENTF_UP;
                        break;
                }
                touch_input->dwMask = 0;
                touch_input->dwTime = 0;
                touch_input->dwExtraInfo = 0;
                touch_input->cxContact = 0;
                touch_input->cyContact = 0;

            } else if (USE_MOUSE && !mouse_used) {

                // disable further mouse inputs this call
                mouse_used = true;

                if (mouse_state.touch_event) {
                    result = true;
                    touch_input->x = mouse_state.pos.x;
                    touch_input->y = mouse_state.pos.y;

                    if (GRAPHICS_WINDOWED) {
                        touch_input->x -= SPICETOUCH_TOUCH_X;
                        touch_input->y -= SPICETOUCH_TOUCH_Y;
                    }

                    // log_misc("wintouchemu", "mouse state ({}, {})", to_string(touch_input->x), to_string(touch_input->y));

                    auto valid = true;
                    if (overlay::OVERLAY) {
                        valid = overlay::OVERLAY->transform_touch_point(
                            &touch_input->x, &touch_input->y);
                    }

                    // touch inputs require 100x precision per pixel
                    touch_input->x *= 100;
                    touch_input->y *= 100;
                    touch_input->hSource = hTouchInput;
                    touch_input->dwID = 0;
                    touch_input->dwFlags = 0;
                    switch (mouse_state.touch_event) {
                        case TOUCHEVENTF_DOWN:
                            if (valid) {
                                touch_input->dwFlags |= TOUCHEVENTF_DOWN;
                            }
                            break;
                        case TOUCHEVENTF_MOVE:
                            if (valid) {
                                touch_input->dwFlags |= TOUCHEVENTF_MOVE;
                            }
                            break;
                        case TOUCHEVENTF_UP:
                            // don't check valid so that this touch ID can be released
                            touch_input->dwFlags |= TOUCHEVENTF_UP;
                            break;
                    }
                    touch_input->dwMask = 0;
                    touch_input->dwTime = 0;
                    touch_input->dwExtraInfo = 0;
                    touch_input->cxContact = 0;
                    touch_input->cyContact = 0;

                    // reset it since the event was consumed & propagated as touch
                    mouse_state.touch_event = 0;
                }
            } else if (!GRAPHICS_IIDX_WSUB) {

                /*
                 * For some reason, Nostalgia won't show an active touch point unless a move event
                 * triggers in the same frame. To work around this, we just supply a fake move
                 * event if we didn't update the same pointer ID in the same call.
                 */

                // find touch point which has no associated input event
                TouchPoint *touch_point = nullptr;
                for (auto &tp : TOUCH_POINTS) {
                    bool found = false;
                    for (UINT i = 0; i < cInputs; i++) {
                        if (input > 0 && pInputs[i].dwID == tp.id) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        touch_point = &tp;
                        break;
                    }
                }

                // check if unused touch point was found
                if (touch_point) {

                    // set touch point
                    result = true;
                    touch_input->x = touch_point->x * 100;
                    touch_input->y = touch_point->y * 100;
                    touch_input->hSource = hTouchInput;
                    touch_input->dwID = touch_point->id;
                    touch_input->dwFlags = 0;
                    touch_input->dwFlags |= TOUCHEVENTF_MOVE;
                    touch_input->dwMask = 0;
                    touch_input->dwTime = 0;
                    touch_input->dwExtraInfo = 0;
                    touch_input->cxContact = 0;
                    touch_input->cyContact = 0;
                }
            }
        }

        // return success
        return result;
    }

    void update() {

        // check if initialized
        if (!INITIALIZED) {
            return;
        }

        // no need for hooks if touch is available
        if (!is_emu_enabled()) {
            return;
        }

        // get window handle
        static HWND hWnd = nullptr;
        if (hWnd == nullptr) {

            // start with the active foreground window
            hWnd = GetForegroundWindow();
            auto title = get_window_title(hWnd);

            // if the foreground window does not match the window title start, find a window
            // that does
            if (!string_begins_with(title, WINDOW_TITLE_START)) {
                hWnd = FindWindowBeginsWith(WINDOW_TITLE_START);
                title = get_window_title(hWnd);
            }

            // if a window title end is set, check to see if it matches
            if (WINDOW_TITLE_END.has_value() && !string_ends_with(title.c_str(), WINDOW_TITLE_END.value().c_str())) {
                hWnd = nullptr;
                title = "";

                for (auto &window : find_windows_beginning_with(WINDOW_TITLE_START)) {
                    auto check_title = get_window_title(window);
                    if (string_ends_with(check_title.c_str(), WINDOW_TITLE_END.value().c_str())) {
                        hWnd = std::move(window);
                        title = std::move(check_title);
                        break;
                    }
                }
            }

            // check window
            if (hWnd == nullptr) {
                return;
            }

            // check if windowed
            if (GRAPHICS_WINDOWED) {
                if (GRAPHICS_IIDX_WSUB) {
                    // no handling is needed here
                    // graphics::MoveWindow_hook will attach hook to windowed subscreen
                    log_info("wintouchemu", "attach touch hook to windowed subscreen for TDJ");
                    USE_MOUSE = false;
                } else if (avs::game::is_model("LDJ") && !GENERIC_SUB_WINDOW_FULLSIZE) {
                    // overlay subscreen in IIDX
                    // use mouse position as ImGui overlay will block the touch window  
                    log_info("wintouchemu", "use mouse cursor API for overlay subscreen");
                    USE_MOUSE = true;
                } else {
                    // create touch window - create overlay if not yet existing at this point
                    log_info("wintouchemu", "create touch window relative to main game window");
                    touch_create_wnd(hWnd, overlay::ENABLED && !overlay::OVERLAY);
                    USE_MOUSE = false;
                }
            } else if (INJECT_MOUSE_AS_WM_TOUCH) {
                log_info(
                    "wintouchemu",
                    "using raw mouse cursor API in full screen and injecting them as WM_TOUCH events");
                USE_MOUSE = true;
            } else {
                log_info("wintouchemu", "activating DirectX hooks");
                // mouse position based input only
                touch_attach_dx_hook();
                USE_MOUSE = false;
            }

            // hooks
            auto GetTouchInputInfo_orig_new = detour::iat_try(
                    "GetTouchInputInfo", GetTouchInputInfoHook, HOOKED_MODULE);
            if (GetTouchInputInfo_orig == nullptr) {
                GetTouchInputInfo_orig = GetTouchInputInfo_orig_new;
            }
        }

        // update touch events
        if (hWnd != nullptr) {

            // get touch events
            TOUCH_EVENTS.clear();
            touch_get_events(TOUCH_EVENTS);

            // get touch points
            TOUCH_POINTS.clear();
            touch_get_points(TOUCH_POINTS);

            // get event count
            auto event_count = TOUCH_EVENTS.size();

            // for the fake move events
            event_count += MAX(0, (int) (TOUCH_POINTS.size() - TOUCH_EVENTS.size()));

            // check if new events are available
            if (event_count > 0) {

                // send fake event to make the game update it's touch inputs
                auto wndProc = (WNDPROC) GetWindowLongPtr(hWnd, GWLP_WNDPROC);
                wndProc(hWnd, WM_TOUCH, MAKEWORD(event_count, 0), (LPARAM) GetTouchInputInfoHook);
            }

            // update frame logging
            if (LOG_FPS) {
                static int log_frames = 0;
                static uint64_t log_time = 0;
                log_frames++;
                if (log_time < get_system_seconds()) {
                    if (log_time > 0) {
                        log_info("wintouchemu", "polling at {} touch frames per second", log_frames);
                    }
                    log_frames = 0;
                    log_time = get_system_seconds();
                }
            }
        }

        // send separate WM_TOUCH event for mouse
        // this must be separate from actual touch events because some games will ignore the return
        // value from GetTouchInputInfo or fail to read dwFlags for valid events, so it's not OK to
        // send empty events when the mouse button is not clicked/released
        if (hWnd != nullptr && USE_MOUSE) {
            bool button_pressed = ((GetKeyState(VK_LBUTTON) & 0x100) != 0);

            // figure out what kind of touch event to simulate
            if (button_pressed && !mouse_state.last_button_pressed) {
                mouse_state.touch_event = TOUCHEVENTF_DOWN;
            } else if (button_pressed && mouse_state.last_button_pressed) {
                mouse_state.touch_event = TOUCHEVENTF_MOVE;
            } else if (!button_pressed && mouse_state.last_button_pressed) {
                mouse_state.touch_event = TOUCHEVENTF_UP;
            }

            mouse_state.last_button_pressed = button_pressed;
            if (mouse_state.touch_event) {
                GetCursorPos(&mouse_state.pos);
                // send fake event to make the game update it's touch inputs
                auto wndProc = (WNDPROC) GetWindowLongPtr(hWnd, GWLP_WNDPROC);
                wndProc(hWnd, WM_TOUCH, MAKEWORD(1, 0), (LPARAM) GetTouchInputInfoHook);
            }
        }
    }
}
 