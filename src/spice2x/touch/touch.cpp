
// enable touch functions - set version to windows 7
// mingw otherwise doesn't load touch stuff
#define _WIN32_WINNT 0x0601

#include <thread>
#include <mutex>
#include <windowsx.h>

#include "touch.h"

#include "avs/game.h"
#include "external/imgui/imgui.h"
#include "hooks/graphics/graphics.h"
#include "misc/eamuse.h"
#include "overlay/overlay.h"
#include "rawinput/touch.h"
#include "util/circular_buffer.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#include "handler.h"
#include "win7.h"
#include "win8.h"

// constants
const char *SPICETOUCH_CLASS_NAME = "spiceTouchClass";
const char *SPICETOUCH_FONT_NAME = "Courier New";
const char *INSERT_CARD_TEXT = "Insert Card";

// settings
static const int TOUCH_EVENT_BUFFER_SIZE = 1024 * 4;
static const int TOUCH_EVENT_BUFFER_THRESHOLD1 = 1024 * 2;
static const int TOUCH_EVENT_BUFFER_THRESHOLD2 = 1024 * 3;

// in mainline spicetools, this was false (show by default)
// in spice2x, this is true (hide by default)
bool SPICETOUCH_CARD_DISABLE = true;
HWND SPICETOUCH_TOUCH_HWND = nullptr;
int SPICETOUCH_TOUCH_X = 0;
int SPICETOUCH_TOUCH_Y = 0;
int SPICETOUCH_TOUCH_WIDTH = 0;
int SPICETOUCH_TOUCH_HEIGHT = 0;

// touch states
std::vector<TouchPoint> TOUCH_POINTS;
std::mutex TOUCH_POINTS_M;
static circular_buffer<TouchEvent> TOUCH_EVENTS(TOUCH_EVENT_BUFFER_SIZE);
std::mutex TOUCH_EVENTS_M;

// general states
static bool SPICETOUCH_INITIALIZED = false;
static bool SPICETOUCH_ATTACHED = false;
static bool SPICETOUCH_ATTACHED_DXHOOK = false;
static HWND SPICETOUCH_HWND = nullptr;
static WNDPROC SPICETOUCH_OLD_PROC = nullptr;
static bool SPICETOUCH_ENABLE_MOUSE = true;
static bool SPICETOUCH_REGISTERED_TOUCH = false;
static bool SPICETOUCH_CALL_OLD_PROC = false;
static std::thread *SPICETOUCH_TOUCH_THREAD = nullptr;
static HFONT SPICETOUCH_FONT;
static RECT SPICETOUCH_CARD_RECT;
static bool SPICETOUCH_CARD_ENABLED = false;
static const char *LOG_MODULE_NAME = "touch";

static TouchHandler *TOUCH_HANDLER = nullptr;

TouchHandler::TouchHandler(std::string name) {
    log_info("touch", "Using touch handler: {}", name);
}

class RawInputTouchHandler : public TouchHandler {
public:
    RawInputTouchHandler() : TouchHandler("rawinput") {
    }
    virtual bool window_register(HWND hWnd) override {
        return true;
    }
    virtual bool window_unregister(HWND hWnd) override {
        return true;
    }
    virtual void handle_message(msg_handler_result&, HWND, UINT, WPARAM, LPARAM) override {
    }
};

/*
 * Add touch event but take care of buffer size
 * Be careful, this doesn't lock the mutex on it's own
 */
void add_touch_event(TouchEvent *te) {

    // check if first threshold is passed
    if (TOUCH_EVENTS.size() > TOUCH_EVENT_BUFFER_THRESHOLD1) {
        switch (te->type) {
            case TOUCH_DOWN:

                // ignore touch down events after first threshold
                return;

            case TOUCH_MOVE:

                // add move event if we're not over the second threshold
                if (TOUCH_EVENTS.size() <= TOUCH_EVENT_BUFFER_THRESHOLD2) {
                    TOUCH_EVENTS.put(*te);
                }

                return;

            case TOUCH_UP:

                // check if buffer is full
                if (TOUCH_EVENTS.full()) {

                    // ignore event
                    return;
                }

                // when the buffer isn't full yet, add the touch up event
                TOUCH_EVENTS.put(*te);
                return;

            default:
                return;
        }
    }

    // add the touch up event
    TOUCH_EVENTS.put(*te);
}

static void touch_initialize() {

    // check if already initialized
    if (SPICETOUCH_INITIALIZED) {
        return;
    }
    SPICETOUCH_INITIALIZED = true;

    // initialize handler
    if (RI_MGR && rawinput::touch::is_enabled(RI_MGR.get())) {
        TOUCH_HANDLER = new RawInputTouchHandler();
    } else if (Win8Handler::is_available()) {
        TOUCH_HANDLER = new Win8Handler();
    } else if (Win7Handler::is_available()) {
        TOUCH_HANDLER = new Win7Handler();
    } else {
        log_warning(LOG_MODULE_NAME, "no touch handler available");
    }
}

static inline void touch_register_window(HWND hWnd) {

    // register touch handling for window
    if (TOUCH_HANDLER != nullptr) {
        TOUCH_HANDLER->window_register(hWnd);
    }
}

static inline void touch_unregister_window(HWND hWnd) {

    // unregister touch handling for window
    if (TOUCH_HANDLER != nullptr) {
        TOUCH_HANDLER->window_unregister(hWnd);
    }
}

bool is_touch_available() {

    // initialize touch handler
    touch_initialize();

    // Check if a touch handler has been set. No need to call `is_available` here
    // as `touch_initialize` does that.
    return TOUCH_HANDLER != nullptr;
}

void update_card_button() {

    // check if enabled
    if (!SPICETOUCH_CARD_ENABLED) {
        return;
    }

    // check touch points
    for (TouchPoint touchPoint : TOUCH_POINTS) {
        POINT pt {};
        pt.x = touchPoint.x;
        pt.y = touchPoint.y;

        if (PtInRect(&SPICETOUCH_CARD_RECT, pt) != 0) {
            eamuse_card_insert(0);
        }
    }
}

static LRESULT CALLBACK SpiceTouchWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    // check if touch was registered
    if (!SPICETOUCH_REGISTERED_TOUCH) {
        SPICETOUCH_REGISTERED_TOUCH = true;

        // check if touch is available
        if (is_touch_available()) {

            // notify the handler of our window
            TOUCH_HANDLER->window_register(hWnd);

            // enable card unless the feature is disabled
            if (!SPICETOUCH_CARD_DISABLE) {
                SPICETOUCH_CARD_ENABLED = true;
            }
        }
    }

    // update rawinput touch display resolution
    if (msg == WM_DISPLAYCHANGE) {
        log_info("touch", "detected display mode change");
        rawinput::touch::display_update();
    }

    if (msg == WM_CLOSE) { 
        if ((GRAPHICS_IIDX_WSUB && hWnd == TDJ_SUBSCREEN_WINDOW) || (hWnd == SDVX_SUBSCREEN_WINDOW)) {
            log_misc("touch", "ignore WM_CLOSE for subscreen window");
            return false;
        }
    }

    // check messages for dedicated window
    if (SPICETOUCH_TOUCH_THREAD != nullptr) {

        // check if overlay is enabled
        auto overlay_enabled = overlay::OVERLAY
                && overlay::OVERLAY->renderer == overlay::OverlayRenderer::SOFTWARE;

        // we don't want mouse events to interfere
        SPICETOUCH_ENABLE_MOUSE = !overlay_enabled || !overlay::OVERLAY->get_active();

        switch (msg) {
            case WM_MOUSEACTIVATE: {
                // Set the main window as the active window to reactivate the imgui cursor
                HWND parent = GetParent(hWnd);
                if (GetActiveWindow() != parent) {
                    SetActiveWindow(parent);
                }
                return MA_NOACTIVATE;
            }
            case WM_SETCURSOR: {

                // set cursor back to the overlay one
                if (overlay_enabled && LOWORD(lParam) == HTCLIENT && overlay::OVERLAY->update_cursor()) {
                    //return true; TODO: can make cursor invisible?
                }
                break;
            }
            case WM_TIMER: {
                InvalidateRect(hWnd, NULL, TRUE);
                break;
            }
            case WM_PAINT: {

                // get window rect
                HWND parent = GetParent(hWnd);
                RECT windowRect {}, clientRect {};
                GetWindowRect(parent, &windowRect);
                GetClientRect(parent, &clientRect);

                // adjust to client area
                POINT p1 {.x = clientRect.left, .y = clientRect.top};
                POINT p2 {.x = clientRect.right, .y = clientRect.bottom};
                ClientToScreen(parent, &p1);
                ClientToScreen(parent, &p2);
                windowRect.left = p1.x;
                windowRect.top = p1.y;
                windowRect.right = p2.x;
                windowRect.bottom = p2.y;

                // check if rect needs to update
                RECT windowRectOld {};
                GetWindowRect(hWnd, &windowRectOld);
                if (memcmp(&windowRectOld, &windowRect, sizeof(RECT)) != 0) {
                    SetWindowPos(hWnd, HWND_TOP,
                            windowRect.left, windowRect.top,
                            windowRect.right - windowRect.left,
                            windowRect.bottom - windowRect.top,
                            SWP_NOZORDER | SWP_NOREDRAW | SWP_NOREPOSITION | SWP_NOACTIVATE);
                }

                // draw overlay
                if (overlay_enabled) {

                    // update and render
                    overlay::OVERLAY->update();
                    overlay::OVERLAY->new_frame();
                    overlay::OVERLAY->render();

                    // get pixel data
                    int width, height;
                    uint32_t *pixel_data = overlay::OVERLAY.get()->sw_get_pixel_data(&width, &height);
                    if (width > 0 && height > 0) {

                        // create bitmap
                        HBITMAP bitmap = CreateBitmap(width, height, 1, 8 * sizeof(uint32_t), pixel_data);

                        // prepare paint
                        PAINTSTRUCT paint {};
                        HDC hdc = BeginPaint(hWnd, &paint);
                        HDC hdcMem = CreateCompatibleDC(hdc);
                        SetBkMode(hdc, TRANSPARENT);

                        /*
                         * draw bitmap
                         * - this currently sets the background to black because of SRCCOPY
                         * - SRCPAINT will blend but colors are wrong
                         * - once this is figured out we could also try hooking WM_PAINT and
                         *   draw directly to the game window
                         */
                        SelectObject(hdcMem, bitmap);
                        BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

                        // clean up
                        DeleteObject(bitmap);
                        DeleteDC(hdcMem);
                        EndPaint(hWnd, &paint);
                    }
                }

                // draw card input
                if (SPICETOUCH_CARD_ENABLED && (SPICETOUCH_FONT != nullptr)) {

                    // prepare paint
                    PAINTSTRUCT paint {};
                    HDC hdc = BeginPaint(hWnd, &paint);
                    SetBkMode(hdc, TRANSPARENT);

                    // create brushes
                    HBRUSH brushBorder = CreateSolidBrush(RGB(0, 196, 0));
                    HBRUSH brushFill = CreateSolidBrush(RGB(255, 192, 203));

                    // get window rect
                    RECT windowRect {};
                    GetWindowRect(hWnd, &windowRect);

                    bool should_rotate = avs::game::is_model({ "J44", "K44" });

                    // create box rect
                    RECT boxRect {};
                    if (should_rotate) {
                        boxRect.left = windowRect.right - 75;
                        boxRect.top = 20;
                        boxRect.right = windowRect.right - 44;
                        boxRect.bottom = 151;
                    } else {
                        boxRect.left = 20;
                        boxRect.top = 44;
                        boxRect.right = 141;
                        boxRect.bottom = 75;
                    }

                    // save box rect for touch input
                    SPICETOUCH_CARD_RECT = boxRect;

                    // draw borders
                    FillRect(hdc, &boxRect, brushBorder);

                    // modify box rect
                    boxRect.left += 1;
                    boxRect.top += 1;
                    boxRect.right -= 1;
                    boxRect.bottom -= 1;

                    // fill box
                    FillRect(hdc, &boxRect, brushFill);

                    // modify box rect
                    if (should_rotate) {
                        boxRect.left += 5 + 20;
                        boxRect.top += 5;
                    } else {
                        boxRect.left += 5;
                        boxRect.top += 5;
                    }

                    // draw text
                    SelectObject(hdc, SPICETOUCH_FONT);
                    SetTextColor(hdc, RGB(0, 196, 0));
                    DrawText(hdc, INSERT_CARD_TEXT, -1, &boxRect, DT_LEFT | DT_BOTTOM | DT_NOCLIP);

                    // delete objects
                    DeleteObject(brushFill);
                    DeleteObject(brushBorder);

                    // end paint
                    EndPaint(hWnd, &paint);
                    return 0;
                }

                // call default window procedure
                return DefWindowProc(hWnd, msg, wParam, lParam);
            }
            case WM_CREATE: {

                // set to layered window
                SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);

                // set alpha value
                SetLayeredWindowAttributes(hWnd, 0, 0xFFu, LWA_ALPHA);
                SetLayeredWindowAttributes(hWnd, RGB(255, 192, 203), 0, LWA_COLORKEY);

                // get window rect
                RECT windowRect {};
                GetWindowRect(hWnd, &windowRect);

                bool should_rotate = avs::game::is_model({ "J44", "K44" });
                auto rotation = should_rotate ? 2700 : 0;

                // load font
                SPICETOUCH_FONT = CreateFont(20, 0, rotation, rotation, FW_NORMAL, FALSE, FALSE, FALSE,
                                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             NONANTIALIASED_QUALITY, DEFAULT_PITCH,
                                             SPICETOUCH_FONT_NAME);
                if (SPICETOUCH_FONT == nullptr) {
                    log_warning(LOG_MODULE_NAME, "font '{}' could not be loaded", SPICETOUCH_FONT_NAME);
                }

                return 0;
            }
            case WM_DESTROY: {
                PostQuitMessage(0);
                return 0;
            }
            default:
                break;
        }
    }

    msg_handler_result result {
        .action = ACTION_PASS,
        .return_value = 0,
    };

    // check if imgui is handling this mouse event
    if (overlay::OVERLAY != nullptr && overlay::OVERLAY->get_active() && ImGui::GetIO().WantCaptureMouse) {
        result.action = ACTION_RETURN_DEFAULT;

    } else if (TOUCH_HANDLER != nullptr) {

        // call touch handler
        TOUCH_HANDLER->handle_message(result, hWnd, msg, wParam, lParam);
    }

    if (result.action == ACTION_RETURN_STORED) {

        // return the value from the touch handler
        return result.return_value;
    }

    if (result.action == ACTION_PASS) {

        // parse mouse messages
        switch (msg) {
            case WM_LBUTTONDOWN: {

                // check if mouse is enabled
                if (SPICETOUCH_ENABLE_MOUSE) {

                    // lock touch points
                    std::lock_guard<std::mutex> lock_points(TOUCH_POINTS_M);
                    std::lock_guard<std::mutex> lock_events(TOUCH_EVENTS_M);

                    // remove all points with ID 0
                    for (size_t x = 0; x < TOUCH_POINTS.size(); x++) {
                        TouchPoint *tp = &TOUCH_POINTS[x];

                        if (tp->id == 0u) {

                            // generate touch up event
                            TouchEvent te {
                                .id = tp->id,
                                .x = tp->x,
                                .y = tp->y,
                                .type = TOUCH_UP,
                                .mouse = tp->mouse,
                            };
                            add_touch_event(&te);

                            // erase touch point
                            TOUCH_POINTS.erase(TOUCH_POINTS.begin() + x);
                        }
                    }

                    // create touch point
                    TouchPoint tp {
                        .id = 0,
                        .x = GET_X_LPARAM(lParam),
                        .y = GET_Y_LPARAM(lParam),
                        .mouse = true,
                    };
                    TOUCH_POINTS.push_back(tp);

                    // add touch down event
                    TouchEvent te {
                        .id = tp.id,
                        .x = tp.x,
                        .y = tp.y,
                        .type = TOUCH_DOWN,
                        .mouse = tp.mouse,
                    };
                    add_touch_event(&te);

                    // card button
                    update_card_button();
                }

                break;
            }
            case WM_MOUSEMOVE: {

                // check if mouse is enabled
                if (SPICETOUCH_ENABLE_MOUSE) {

                    // lock touch points
                    std::lock_guard<std::mutex> lock_points(TOUCH_POINTS_M);
                    std::lock_guard<std::mutex> lock_events(TOUCH_EVENTS_M);

                    // update point
                    for (auto &x : TOUCH_POINTS) {
                        TouchPoint *tp = &x;

                        // find ID 0
                        if (tp->id == 0u) {

                            // update touch point position
                            tp->x = GET_X_LPARAM(lParam);
                            tp->y = GET_Y_LPARAM(lParam);

                            // add touch move event
                            TouchEvent te {
                                .id = tp->id,
                                .x = tp->x,
                                .y = tp->y,
                                .type = TOUCH_MOVE,
                                .mouse = tp->mouse,
                            };
                            add_touch_event(&te);

                            break;
                        }
                    }
                }

                break;
            }
            case WM_LBUTTONUP: {

                // check if mouse is enabled
                if (SPICETOUCH_ENABLE_MOUSE) {

                    // lock touch points
                    std::lock_guard<std::mutex> lock_points(TOUCH_POINTS_M);
                    std::lock_guard<std::mutex> lock_events(TOUCH_EVENTS_M);

                    // remove all points with ID 0
                    for (size_t x = 0; x < TOUCH_POINTS.size(); x++) {
                        TouchPoint *tp = &TOUCH_POINTS[x];

                        if (tp->id == 0u) {

                            // generate touch up event
                            TouchEvent te {
                                .id = tp->id,
                                .x = tp->x,
                                .y = tp->y,
                                .type = TOUCH_UP,
                                .mouse = tp->mouse,
                            };
                            add_touch_event(&te);

                            // remove touch point
                            TOUCH_POINTS.erase(TOUCH_POINTS.begin() + x);
                        }
                    }
                }

                break;
            }
            default:

                // call original function
                if (SPICETOUCH_CALL_OLD_PROC && SPICETOUCH_OLD_PROC != nullptr) {
                    return SPICETOUCH_OLD_PROC(hWnd, msg, wParam, lParam);
                }
        }
    }

    // clean up
    if (SPICETOUCH_ATTACHED_DXHOOK) {
        return 0;
    } else {
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

void touch_attach_wnd(HWND hWnd) {

    // check if already attached
    if (SPICETOUCH_ATTACHED) {
        return;
    }
    SPICETOUCH_ATTACHED = true;

    // initialize touch handler
    touch_initialize();
    touch_register_window(hWnd);

    // hook window process
    SPICETOUCH_HWND = hWnd;
    SPICETOUCH_OLD_PROC = (WNDPROC) SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR) SpiceTouchWndProc);

    // update rawinput
    rawinput::touch::display_update();
}

void touch_attach_dx_hook() {

    // check if already attached
    if (SPICETOUCH_ATTACHED) {
        return;
    }
    SPICETOUCH_ATTACHED = true;
    SPICETOUCH_ATTACHED_DXHOOK = true;

    // initialize touch handler
    touch_initialize();

    // add dx hook
    graphics_add_wnd_proc(SpiceTouchWndProc);

    // update rawinput
    rawinput::touch::display_update();
}

void touch_create_wnd(HWND hWnd, bool overlay) {

    // check if already attached
    if (SPICETOUCH_ATTACHED) {
        return;
    }
    SPICETOUCH_ATTACHED = true;

    // initialize touch handler
    touch_initialize();

    // create thread
    SPICETOUCH_TOUCH_THREAD = new std::thread([hWnd, overlay]() {

        // create class
        WNDCLASSEX wndClass {};
        wndClass.cbSize = sizeof(WNDCLASSEX);
        wndClass.style = 3;
        wndClass.lpfnWndProc = SpiceTouchWndProc;
        wndClass.cbClsExtra = 0;
        wndClass.cbWndExtra = 0;
        wndClass.hInstance = GetModuleHandle(nullptr);
        wndClass.hIcon = nullptr;
        wndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wndClass.hbrBackground = CreateSolidBrush(RGB(255, 192, 203));
        wndClass.lpszMenuName = nullptr;
        wndClass.lpszClassName = SPICETOUCH_CLASS_NAME;
        wndClass.hIconSm = nullptr;

        // register class
        if (!RegisterClassExA(&wndClass)) {
            log_warning(LOG_MODULE_NAME, "failed to register SpiceTouch class: {}", GetLastError());
            return;
        }

        // calculate touch window dimensions 
        update_spicetouch_window_dimensions(hWnd);

        // create window
        HWND touch_window = CreateWindowExA(
                0,
                SPICETOUCH_CLASS_NAME,
                "SpiceTools Touch",
                (DWORD) CW_USEDEFAULT,
                SPICETOUCH_TOUCH_X,
                SPICETOUCH_TOUCH_Y,
                SPICETOUCH_TOUCH_WIDTH,
                SPICETOUCH_TOUCH_HEIGHT,
                hWnd,
                nullptr,
                GetModuleHandle(nullptr),
                nullptr
        );

        log_misc(
            LOG_MODULE_NAME, "create SpiceTouch window ({}x{} @ {}, {})", 
            SPICETOUCH_TOUCH_WIDTH, SPICETOUCH_TOUCH_HEIGHT, SPICETOUCH_TOUCH_X, SPICETOUCH_TOUCH_Y);

        // check window
        if (touch_window == nullptr) {
            log_warning(LOG_MODULE_NAME, "failed to create SpiceTouch window: {}", GetLastError());
            return;
        }

        // save reference
        SPICETOUCH_TOUCH_HWND = touch_window;

        // window settings
        ShowWindow(touch_window, SW_SHOWNOACTIVATE);
        UpdateWindow(touch_window);

        // register
        touch_register_window(touch_window);

        // overlay
        if (overlay && overlay::ENABLED) {
            if (overlay::OVERLAY) {
                log_warning(LOG_MODULE_NAME, "requested overlay, but already existing");
            } else {

                // create instance
                overlay::OVERLAY.reset(new overlay::SpiceOverlay(touch_window));

                // draw overlay in 30 FPS
                SetTimer(touch_window, 1, 1000 / 30, NULL);
            }
        }

        // window loop
        MSG msg {};
        while (GetMessage(&msg, nullptr, 0, 0) && SPICETOUCH_TOUCH_THREAD != nullptr) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // kill overlay
        if (overlay) {
            overlay::OVERLAY.reset();
        }

        // unregister
        touch_unregister_window(touch_window);
        log_misc(LOG_MODULE_NAME, "window closed");
    });

    // update rawinput
    rawinput::touch::display_update();
}

void touch_detach() {

    // remove window process hook
    if (SPICETOUCH_HWND != nullptr) {
        touch_unregister_window(SPICETOUCH_HWND);

        SetWindowLongPtr(SPICETOUCH_HWND, GWLP_WNDPROC, (LONG_PTR) SPICETOUCH_OLD_PROC);
        SPICETOUCH_HWND = nullptr;
        SPICETOUCH_OLD_PROC = nullptr;
    }

    // remove dx hook
    graphics_remove_wnd_proc(SpiceTouchWndProc);
    SPICETOUCH_TOUCH_THREAD = nullptr;
    SPICETOUCH_ATTACHED_DXHOOK = false;
    SPICETOUCH_ATTACHED = false;
}

void touch_write_points(std::vector<TouchPoint> *touch_points) {

    // check size first
    if (touch_points->empty()) {
        return;
    }

    // lock
    std::lock_guard<std::mutex> points(TOUCH_POINTS_M);
    std::lock_guard<std::mutex> events(TOUCH_EVENTS_M);

    // iterate through all the provided touch points
    for (auto &tp : *touch_points) {

        // find touch point to update
        bool found = false;
        for (auto &arr_tp : TOUCH_POINTS) {
            if (arr_tp.id == tp.id) {

                // mark as found
                found = true;

                // update position
                arr_tp.x = tp.x;
                arr_tp.y = tp.y;

                // add touch move event
                TouchEvent te {
                    .id = tp.id,
                    .x = tp.x,
                    .y = tp.y,
                    .type = TOUCH_MOVE,
                    .mouse = tp.mouse,
                };
                add_touch_event(&te);
            }
        }

        // create new touch point when not found
        if (!found) {

            // add touch point
            TOUCH_POINTS.push_back(tp);

            // add touch down event
            TouchEvent te {
                .id = tp.id,
                .x = tp.x,
                .y = tp.y,
                .type = TOUCH_DOWN,
                .mouse = tp.mouse,
            };
            add_touch_event(&te);
        }
    }
}

void touch_remove_points(std::vector<DWORD> *touch_point_ids) {

    // check size first
    if (touch_point_ids->empty()) {
        return;
    }

    // lock
    std::lock_guard<std::mutex> points(TOUCH_POINTS_M);
    std::lock_guard<std::mutex> events(TOUCH_EVENTS_M);

    // find the touch points to remove
    for (auto id : *touch_point_ids) {
        for (size_t x = 0; x < TOUCH_POINTS.size(); x++) {
            TouchPoint *tp = &TOUCH_POINTS[x];

            // check if the IDs match
            if (tp->id == id) {

                // add touch up event
                TouchEvent te {
                    .id = id,
                    .x = tp->x,
                    .y = tp->y,
                    .type = TOUCH_UP,
                    .mouse = tp->mouse,
                };
                add_touch_event(&te);

                // delete touch point
                TOUCH_POINTS.erase(TOUCH_POINTS.begin() + x);
                break;
            }
        }
    }
}

void touch_get_points(std::vector<TouchPoint> &touch_points, bool overlay) {

    // update timeouts
    if (RI_MGR) {
        rawinput::touch::update_timeouts(RI_MGR.get());
    }

    // overlay override
    if (!overlay &&
        overlay::OVERLAY &&
        overlay::OVERLAY->get_active() &&
        !overlay::OVERLAY->can_transform_touch_input() &&
        ImGui::GetIO().WantCaptureMouse) {

        return;
    }

    // lock
    std::lock_guard<std::mutex> lock(TOUCH_POINTS_M);

    // append touch points
    touch_points.insert(touch_points.end(), TOUCH_POINTS.begin(), TOUCH_POINTS.end());
}

void touch_get_events(std::vector<TouchEvent> &touch_events, bool overlay) {

    // update timeouts
    if (RI_MGR) {
        rawinput::touch::update_timeouts(RI_MGR.get());
    }

    // lock
    std::lock_guard<std::mutex> lock(TOUCH_EVENTS_M);

    // overlay override
    if (!overlay &&
        overlay::OVERLAY &&
        overlay::OVERLAY->get_active() &&
        !overlay::OVERLAY->can_transform_touch_input() &&
        ImGui::GetIO().WantCaptureMouse) {

        TOUCH_EVENTS.reset();
        return;
    }

    // append touch points
    while (!TOUCH_EVENTS.empty()) {
        touch_events.push_back(TOUCH_EVENTS.get());
    }
}

void update_spicetouch_window_dimensions(HWND hWnd) {
    RECT clientRect {};
    GetClientRect(hWnd, &clientRect);

    // adjust to client area
    POINT topleft {.x = 0, .y = 0};
    ClientToScreen(hWnd, &topleft);
    POINT bottomright {.x = clientRect.right, .y = clientRect.bottom};
    ClientToScreen(hWnd, &bottomright);

    SPICETOUCH_TOUCH_X = topleft.x;
    SPICETOUCH_TOUCH_Y = topleft.y;
    SPICETOUCH_TOUCH_WIDTH = bottomright.x - topleft.x;
    SPICETOUCH_TOUCH_HEIGHT = bottomright.y - topleft.y;
}