// enable Windows 8 touch injection types; the functions are loaded dynamically
#define _WIN32_WINNT 0x0602

#include <atomic>
#include <mutex>

#include <windows.h>
#include <commctrl.h>

#include "inject.h"
#include "inject_internal.h"

#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

namespace nativetouch_inject {

    constexpr DWORD INJECTION_RETRY_DELAY_MS = 1;
    constexpr POINTER_FLAGS CONTACT_DOWN_FLAGS =
        POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
    constexpr POINTER_FLAGS CONTACT_UPDATE_FLAGS =
        POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;

    // Windows 8 APIs are resolved dynamically to preserve older-OS compatibility
    static decltype(RegisterTouchWindow) *RegisterTouchWindow_orig = nullptr;
    static decltype(InitializeTouchInjection) *InitializeTouchInjection_ptr = nullptr;
    static decltype(InjectTouchInput) *InjectTouchInput_ptr = nullptr;

    // state for the single synthetic touch contact
    struct ContactState {
        internal::ContactOwner owner = internal::ContactOwner::None;
        HWND input_window = nullptr;
        POINT position {};
        UINT_PTR timer_id = 0;

        bool is_active() const {
            return owner != internal::ContactOwner::None;
        }
    };

    // tracks an injector-owned contact without matching unrelated hardware touches
    struct SyntheticTouchIdentity {
        POINT down_position {};
        HANDLE source = nullptr;
        DWORD id = 0;
        bool identified = false;
        bool pending = false;
        std::atomic<HWND> transform_window { nullptr };

        void begin(POINT position, HWND window) {
            down_position = position;
            source = nullptr;
            id = 0;
            identified = false;
            pending = true;
            transform_window.store(window, std::memory_order_release);
        }

        void reset(HWND expected_window) {
            source = nullptr;
            id = 0;
            identified = false;
            pending = false;
            transform_window.compare_exchange_strong(
                expected_window, nullptr, std::memory_order_acq_rel);
        }

        HWND get_transform_window() const {
            return transform_window.load(std::memory_order_acquire);
        }

        bool matches(PTOUCHINPUT point) {
            // InjectTouchInput provides no application marker in TOUCHINPUT. Claim the first
            // matching DOWN, then use the source and ID assigned by Windows for this contact.
            if (!identified) {
                // correlate the pending injection's pixel position
                constexpr LONG POSITION_TOLERANCE = 200;
                const auto delta_x = point->x - down_position.x * 100;
                const auto delta_y = point->y - down_position.y * 100;
                if (!pending || !(point->dwFlags & TOUCHEVENTF_DOWN) ||
                    delta_x < -POSITION_TOLERANCE || delta_x > POSITION_TOLERANCE ||
                    delta_y < -POSITION_TOLERANCE || delta_y > POSITION_TOLERANCE) {
                    return false;
                }

                // save the identity that remains stable through this contact's MOVE and UP.
                source = point->hSource;
                id = point->dwID;
                identified = true;
            }

            // require both the provider and contact identities to match.
            return point->hSource == source && point->dwID == id;
        }
    };

    static ContactState contact_state;
    static SyntheticTouchIdentity synthetic_touch_identity;

    // main game window that receives WM_TOUCH forwarded from the dedicated TDJ subscreen
    static std::atomic<HWND> touch_delivery_window { nullptr };

    // window whose UI thread owns synthetic contact state and synthetic touch requests;
    // normally the active touch window, while dedicated TDJ uses the subscreen window
    static std::atomic<HWND> injection_window { nullptr };

    static std::once_flag initialization_once;
    static int window_subclass_token;

    bool internal::is_tdj_dedicated_subscreen(HWND window) {
        return GRAPHICS_WINDOWED && GRAPHICS_IIDX_WSUB &&
            window == TDJ_SUBSCREEN_WINDOW;
    }

    // submit one synthetic contact frame to Windows touch injection
    static bool inject_touch_frame(POINT position, POINTER_FLAGS pointer_flags) {
        POINTER_TOUCH_INFO contact {};
        contact.pointerInfo.pointerType = PT_TOUCH;
        contact.pointerInfo.pointerId = 0;
        contact.pointerInfo.pointerFlags = pointer_flags;
        contact.pointerInfo.ptPixelLocation = position;
        contact.touchFlags = TOUCH_FLAG_NONE;

        // ERROR_NOT_READY is transient when frames arrive within the same timing interval
        BOOL result = InjectTouchInput_ptr(1, &contact);
        if (!result && GetLastError() == ERROR_NOT_READY) {
            Sleep(INJECTION_RETRY_DELAY_MS);
            result = InjectTouchInput_ptr(1, &contact);
        }
        if (result) {
            return true;
        }

        static bool error_logged = false;
        if (!error_logged) {
            error_logged = true;
            log_warning("touch::native", "failed to inject synthetic touch input: {}", GetLastError());
        }
        return false;
    }

    // map a screen point into the active dedicated window or subscreen overlay
    bool internal::transform_touch_position(HWND window, POINT *position) {
        // scale the resized IIDX subscreen client area into the game's touch-display coordinates
        if (internal::is_tdj_dedicated_subscreen(window)) {
            RECT client_rect {};
            if (!GetClientRect(window, &client_rect) ||
                client_rect.right <= 0 || client_rect.bottom <= 0 ||
                SPICETOUCH_TOUCH_WIDTH <= 0 || SPICETOUCH_TOUCH_HEIGHT <= 0) {
                return false;
            }

            if (!ScreenToClient(window, position)) {
                return false;
            }
            if (!PtInRect(&client_rect, *position)) {
                return false;
            }
            position->x = SPICETOUCH_TOUCH_X +
                MulDiv(position->x, SPICETOUCH_TOUCH_WIDTH, client_rect.right);
            position->y = SPICETOUCH_TOUCH_Y +
                MulDiv(position->y, SPICETOUCH_TOUCH_HEIGHT, client_rect.bottom);
            return true;
        }

        // leave screen coordinates unchanged when no active overlay transform applies
        if (overlay::OVERLAY == nullptr ||
            !overlay::OVERLAY->get_active() ||
            !overlay::OVERLAY->can_transform_touch_input()) {
            return true;
        }

        // convert physical screen coordinates to the window-relative coordinates the overlay expects
        if (GRAPHICS_WINDOWED) {
            position->x -= SPICETOUCH_TOUCH_X;
            position->y -= SPICETOUCH_TOUCH_Y;
        }

        // ask the overlay to do the game-specific translation
        return overlay::OVERLAY->transform_touch_point(&position->x, &position->y);
    }

    // rewrite only the contact created by this injector into game touch coordinates
    bool transform_touch_input(PTOUCHINPUT point) {
        const auto transform_window = synthetic_touch_identity.get_transform_window();
        if (transform_window == nullptr) {
            return false;
        }

        if (!synthetic_touch_identity.matches(point)) {
            // this contact is not owned by this injector; leave it unchanged
            return false;
        }

        // Windows receives the physical position; the game receives the mapped position
        POINT position { point->x / 100, point->y / 100 };
        if (internal::transform_touch_position(transform_window, &position)) {
            point->x = position.x * 100;
            point->y = position.y * 100;
        }

        if (point->dwFlags & TOUCHEVENTF_UP) {
            synthetic_touch_identity.reset(transform_window);
        }

        return true;
    }

    bool internal::contact_is_active() {
        return contact_state.is_active();
    }

    bool internal::contact_is_owned_by(ContactOwner owner, HWND window) {
        return contact_state.is_active() &&
            contact_state.owner == owner &&
            contact_state.input_window == window;
    }

    bool internal::begin_contact(
            ContactOwner owner,
            HWND window,
            POINT position,
            bool transform_contact) {
        contact_state.owner = owner;
        contact_state.input_window = window;
        contact_state.position = position;
        contact_state.timer_id = 0;
        if (transform_contact) {
            synthetic_touch_identity.begin(position, window);
        }

        if (inject_touch_frame(position, CONTACT_DOWN_FLAGS)) {
            return true;
        }

        contact_state = {};
        if (transform_contact) {
            synthetic_touch_identity.reset(window);
        }
        return false;
    }

    bool internal::update_contact(ContactOwner owner, HWND window, POINT position) {
        if (!internal::contact_is_owned_by(owner, window) ||
            !inject_touch_frame(position, CONTACT_UPDATE_FLAGS)) {
            return false;
        }

        contact_state.position = position;
        return true;
    }

    void internal::set_contact_timer(
            ContactOwner owner, HWND window, UINT_PTR timer_id) {
        if (internal::contact_is_owned_by(owner, window)) {
            contact_state.timer_id = timer_id;
        }
    }

    // end whichever producer currently owns the single synthetic contact
    bool internal::release_active_contact() {
        if (!contact_state.is_active()) {
            return true;
        }

        const auto input_window = contact_state.input_window;
        if (input_window != nullptr && contact_state.timer_id != 0) {
            KillTimer(input_window, contact_state.timer_id);
        }

        const auto result = inject_touch_frame(contact_state.position, POINTER_FLAG_UP);
        contact_state = {};

        // release mouse capture if this contact owns it
        if (input_window != nullptr && GetCapture() == input_window) {
            ReleaseCapture();
        }
        return result;
    }

    // translate primary mouse messages on a touch window into one touch contact
    static LRESULT CALLBACK touch_window_subclass_proc(
            HWND window, UINT message, WPARAM w_param, LPARAM l_param,
            UINT_PTR subclass_id, DWORD_PTR) {

        // IIDX handles touch on its main window even when input belongs to the subscreen
        // forward the touch message there
        if (message == WM_TOUCH && internal::is_tdj_dedicated_subscreen(window)) {
            const auto delivery_window =
                touch_delivery_window.load(std::memory_order_acquire);
            if (delivery_window != nullptr) {
                return SendMessageW(delivery_window, message, w_param, l_param);
            }
        }

        if (internal::handle_synthetic_message(window, message, w_param, l_param) ||
            internal::handle_mouse_message(window, message, w_param)) {
            return 0;
        }

        if (message == WM_NCDESTROY) {
            if (contact_state.input_window == window) {
                internal::release_active_contact();
            }
            HWND expected_window = window;
            injection_window.compare_exchange_strong(
                expected_window, nullptr, std::memory_order_acq_rel);
            touch_delivery_window.store(nullptr, std::memory_order_release);
            RemoveWindowSubclass(window, touch_window_subclass_proc, subclass_id);
        }

        return DefSubclassProc(window, message, w_param, l_param);
    }

    // attach mouse injection without replacing the window's existing procedure
    static void attach_window_impl(HWND window, bool register_touch) {
        internal::initialize_touch_injection();

        if (!internal::touch_injection_available()) {
            return;
        }

        // if requested, register for touch messages (for cases where the game didn't call it)
        if (register_touch &&
            (RegisterTouchWindow_orig == nullptr ||
             !RegisterTouchWindow_orig(window, TWF_WANTPALM))) {
            log_warning(
                "touch::native", "failed to register mouse touch window: {}", GetLastError());
            return;
        }

        // set window subclass to intercept messages
        if (!SetWindowSubclass(
                window,
                touch_window_subclass_proc,
                reinterpret_cast<UINT_PTR>(&window_subclass_token),
                0)) {
            log_warning("touch::native", "failed to attach mouse touch injection to window: {}", GetLastError());
            return;
        }

        // publish the UI-thread target for synthetic touch requests
        if (!GRAPHICS_IIDX_WSUB || window == TDJ_SUBSCREEN_WINDOW) {
            injection_window.store(window, std::memory_order_release);
        }
        log_misc(
            "touch::native", "mouse touch injection attached to window {}", fmt::ptr(window));
    }

    HWND internal::get_injection_window() {
        return injection_window.load(std::memory_order_acquire);
    }

    void attach_window(HWND window) {
        // attach window, but don't register for Windows touch messages
        attach_window_impl(window, false);
    }

    void register_and_attach_window(HWND window) {
        // attach window and register for Windows touch messages
        attach_window_impl(window, true);
    }

    // preserve native registration and attach mouse injection to the touch window
    static BOOL WINAPI RegisterTouchWindowHook(HWND window, ULONG flags) {

        // for TDJ in windowed subscreen mode, the main game window is
        // target for delivering touch messages, not the subscreen
        if (GRAPHICS_IIDX_WSUB && window != TDJ_SUBSCREEN_WINDOW) {
            touch_delivery_window.store(window, std::memory_order_release);
            return TRUE;
        }

        // call original
        const auto result = RegisterTouchWindow_orig(window, flags);

        if (result) {
            // attach but don't register for touch messages
            // (we're already in the middle of RegisterTouchWindow as a result
            // of the game calling it)
            attach_window(window);
        }

        return result;
    }

    static void clear_touch_injection_functions() {
        InitializeTouchInjection_ptr = nullptr;
        InjectTouchInput_ptr = nullptr;
    }

    // load and initialize Windows 8 touch injection without a static API dependency
    void internal::initialize_touch_injection() {
        std::call_once(initialization_once, [] {
            internal::initialize_synthetic_touch();

            // load all APIs from user32 without adding static imports
            const auto user32 = libutils::load_library("user32.dll");
            InitializeTouchInjection_ptr = libutils::try_proc<decltype(InitializeTouchInjection_ptr)>(
                user32, "InitializeTouchInjection");
            InjectTouchInput_ptr = libutils::try_proc<decltype(InjectTouchInput_ptr)>(
                user32, "InjectTouchInput");

            if (InitializeTouchInjection_ptr == nullptr ||
                InjectTouchInput_ptr == nullptr) {
                clear_touch_injection_functions();
                log_warning(
                    "touch::native", "touch injection API unavailable; mouse touch injection disabled");
                return;
            }

            // reserve one synthetic contact and disable Windows' visual touch feedback
            if (!InitializeTouchInjection_ptr(1, TOUCH_FEEDBACK_NONE)) {
                log_warning(
                    "touch::native", "failed to initialize mouse touch injection: {}", GetLastError());
                clear_touch_injection_functions();
                return;
            }

            log_misc("touch::native", "mouse touch injection initialized");
        });
    }

    bool internal::touch_injection_available() {
        return InjectTouchInput_ptr != nullptr;
    }

    // install injection support for touch windows registered by the game module
    void hook(HMODULE module) {
        internal::initialize_touch_injection();

        RegisterTouchWindow_orig = detour::iat_try(
            "RegisterTouchWindow", RegisterTouchWindowHook, module);
        if (RegisterTouchWindow_orig != nullptr) {
            log_misc("touch::native", "RegisterTouchWindow hooked");
        }
    }
}
