// enable touch functions - set version to windows 7
// mingw otherwise doesn't load touch stuff
#define _WIN32_WINNT 0x0601

#include "wintouchemu.h"

#include <optional>

#include "avs/game.h"
#include "games/gitadora/gitadora.h"
#include "games/popn/popn.h"
#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "overlay/windows/generic_sub.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

namespace wintouchemu {

    struct MouseState {
        POINT position {};
        bool last_button_pressed = false;
        DWORD touch_event = 0;
    };

    // settings
    bool FORCE = false;
    bool INJECT_MOUSE_AS_WM_TOUCH = false;
    bool ADD_TOUCH_FLAG_PRIMARY = false;

    static double last_touch_event = 0.0;

    static inline bool is_emu_enabled() {
        return FORCE || !is_touch_available("wintouchemu::is_emu_enabled") || GRAPHICS_SHOW_CURSOR;
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
    bool INITIALIZED = false;
    MouseState mouse_state;

    void hook(const char *window_title, HMODULE module) {

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
        log_misc("wintouchemu", "initializing");
        INITIALIZED = true;
    }

    void hook_title_ends(
            const char *window_title_start,
            const char *window_title_end,
            HMODULE module) {
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
                    RECT client_rect {};
                    GetClientRect(TDJ_SUBSCREEN_WINDOW, &client_rect);
                    x = static_cast<float>(x) / client_rect.right * SPICETOUCH_TOUCH_WIDTH +
                        SPICETOUCH_TOUCH_X;
                    y = static_cast<float>(y) / client_rect.bottom * SPICETOUCH_TOUCH_HEIGHT +
                        SPICETOUCH_TOUCH_Y;
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
                            if (ADD_TOUCH_FLAG_PRIMARY) {
                                touch_input->dwFlags |= TOUCHEVENTF_PRIMARY;
                            }

                            touch_input->dwFlags |= TOUCHEVENTF_DOWN;
                        }
                        break;
                    case TOUCH_MOVE:
                        if (valid) {
                            if (ADD_TOUCH_FLAG_PRIMARY) {
                                touch_input->dwFlags |= TOUCHEVENTF_PRIMARY;
                            }

                            touch_input->dwFlags |= TOUCHEVENTF_MOVE;
                        }
                        break;
                    case TOUCH_UP:
                        // don't check valid so that this touch ID can be released
                        if (ADD_TOUCH_FLAG_PRIMARY) {
                            touch_input->dwFlags |= TOUCHEVENTF_PRIMARY;
                        }

                        touch_input->dwFlags |= TOUCHEVENTF_UP;
                        break;
                }
                touch_input->dwMask = 0;
                touch_input->dwTime = 0;
                touch_input->dwExtraInfo = 0;
                touch_input->cxContact = 0;
                touch_input->cyContact = 0;

            } else if (USE_MOUSE && !mouse_used) {
                mouse_used = true;

                if (mouse_state.touch_event != 0) {
                    result = true;
                    touch_input->x = mouse_state.position.x;
                    touch_input->y = mouse_state.position.y;

                    auto valid = true;
                    if (overlay::OVERLAY) {
                        valid = overlay::OVERLAY->transform_touch_point(
                            &touch_input->x, &touch_input->y);
                    }

                    touch_input->x *= 100;
                    touch_input->y *= 100;
                    touch_input->hSource = hTouchInput;
                    touch_input->dwID = 0;
                    switch (mouse_state.touch_event) {
                        case TOUCHEVENTF_DOWN:
                        case TOUCHEVENTF_MOVE:
                            if (valid) {
                                touch_input->dwFlags = mouse_state.touch_event;
                            }
                            break;
                        case TOUCHEVENTF_UP:
                            touch_input->dwFlags = TOUCHEVENTF_UP;
                            break;
                    }
                    if (ADD_TOUCH_FLAG_PRIMARY && touch_input->dwFlags != 0) {
                        touch_input->dwFlags |= TOUCHEVENTF_PRIMARY;
                    }

                    mouse_state.touch_event = 0;
                }
            } else {

                // beatstream requires a MOVE for active points in each update
                // add one for every active point without a matching input event
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

            if (WINDOW_TITLE_END.has_value() &&
                !string_ends_with(title.c_str(), WINDOW_TITLE_END.value().c_str())) {
                hWnd = nullptr;
                for (const auto window : find_windows_beginning_with(WINDOW_TITLE_START)) {
                    const auto check_title = get_window_title(window);
                    if (string_ends_with(
                            check_title.c_str(), WINDOW_TITLE_END.value().c_str())) {
                        hWnd = window;
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
                    log_info("wintouchemu", "attach touch hook to windowed TDJ subscreen");
                    USE_MOUSE = false;
                } else if (avs::game::is_model("LDJ") && !GENERIC_SUB_WINDOW_FULLSIZE) {
                    log_info("wintouchemu", "use mouse cursor API for IIDX overlay subscreen");
                    USE_MOUSE = true;
                } else if (games::popn::is_pikapika_model()) {
                    log_info("wintouchemu", "use mouse cursor API for pop'n overlay subscreen");
                    USE_MOUSE = true;
                } else if (games::gitadora::is_arena_model() &&
                           GRAPHICS_PREVENT_SECONDARY_WINDOWS) {
                    log_info("wintouchemu", "use mouse cursor API for gitadora overlay subscreen");
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
                    "using raw mouse cursor API in full screen and injecting WM_TOUCH");
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

        const auto now = get_performance_milliseconds();

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
                last_touch_event = now;

                // send fake event to make the game update it's touch inputs
                auto wndProc = (WNDPROC) GetWindowLongPtr(hWnd, GWLP_WNDPROC);
                wndProc(hWnd, WM_TOUCH, MAKEWORD(event_count, 0), (LPARAM) GetTouchInputInfoHook);
            }

        }

        if (hWnd != nullptr && USE_MOUSE) {
            auto button_pressed = get_async_primary_mouse();
            if (button_pressed && now - last_touch_event < 500) {
                button_pressed = false;
            }

            if (button_pressed && !mouse_state.last_button_pressed) {
                mouse_state.touch_event = TOUCHEVENTF_DOWN;
            } else if (button_pressed) {
                mouse_state.touch_event = TOUCHEVENTF_MOVE;
            } else if (mouse_state.last_button_pressed) {
                mouse_state.touch_event = TOUCHEVENTF_UP;
            }

            mouse_state.last_button_pressed = button_pressed;
            if (mouse_state.touch_event != 0 && GetCursorPos(&mouse_state.position)) {
                const auto window_proc = reinterpret_cast<WNDPROC>(
                    GetWindowLongPtr(hWnd, GWLP_WNDPROC));
                window_proc(
                    hWnd,
                    WM_TOUCH,
                    MAKEWORD(1, 0),
                    reinterpret_cast<LPARAM>(GetTouchInputInfoHook));
            }
        }
    }
}
 