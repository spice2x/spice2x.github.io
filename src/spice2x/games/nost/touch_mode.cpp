#include "touch_mode.h"

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

#include "touch/native/nativetouchhook.h"
#include "touch/touch.h"
#include "util/logging.h"

namespace games::nost::touch_mode {

    struct ButtonBounds {
        HWND window = nullptr;
        RECT client {};
        LONG client_width = 0;
        LONG client_height = 0;
        bool valid = false;
    };

    static std::atomic_bool enabled_state { false };
    static std::atomic<Mode> current_mode_state { Mode::Touch };
    static std::mutex state_mutex;
    static ButtonBounds button_bounds;

    // observed/button contacts drive the overlay activation, while suppressed
    // contacts retain ownership of native events that must not reach the game
    static std::unordered_set<DWORD> observed_contacts;
    static std::unordered_set<DWORD> button_contacts;
    static std::unordered_set<DWORD> suppressed_button_contacts;
    static bool toggle_pending = false;

    // mirror PAN's hardware contacts into the shared client-space touch list so
    // the overlay button and piano input consume the same contact lifecycle
    static void publish_native_touch(const TOUCHINPUT &point, bool synthetic) {
        if (synthetic) {
            return;
        }

        if (point.dwFlags & TOUCHEVENTF_UP) {
            std::vector<DWORD> ids { point.dwID };
            touch_remove_points(&ids);
            return;
        }
        if (!(point.dwFlags & (TOUCHEVENTF_DOWN | TOUCHEVENTF_MOVE))) {
            return;
        }

        HWND window = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            window = button_bounds.window;
        }
        if (window == nullptr) {
            return;
        }

        POINT position { point.x / 100, point.y / 100 };
        if (!ScreenToClient(window, &position)) {
            return;
        }

        std::vector<TouchPoint> touch_points {{
            .id = point.dwID,
            .x = position.x,
            .y = position.y,
        }};
        touch_write_points(&touch_points);
    }

    // hardware contacts arrive in screen coordinates, while synthetic contacts
    // have already been transformed into game-client coordinates
    static bool native_touch_in_button(const TOUCHINPUT &point, bool synthetic) {
        if (!button_bounds.valid) {
            return false;
        }

        POINT position { point.x / 100, point.y / 100 };
        if (!synthetic && !ScreenToClient(button_bounds.window, &position)) {
            return false;
        }
        return PtInRect(&button_bounds.client, position) != FALSE;
    }

    void enable() {
        if (enabled_state.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            current_mode_state.store(Mode::Touch, std::memory_order_release);
            button_bounds = {};
            observed_contacts.clear();
            button_contacts.clear();
            suppressed_button_contacts.clear();
            toggle_pending = false;
        }

        nativetouch::set_input_filter(filter_native_touch);
        log_info("nost::touch", "enabled (v4)");
    }

    void disable() {
        if (!enabled_state.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        nativetouch::set_input_filter(nullptr);

        std::vector<TouchPoint> touch_points;
        std::vector<DWORD> touch_point_ids;
        touch_get_points(touch_points, true);
        for (const auto &point : touch_points) {
            touch_point_ids.push_back(point.id);
        }
        touch_remove_points(&touch_point_ids);

        std::lock_guard<std::mutex> lock(state_mutex);
        current_mode_state.store(Mode::Touch, std::memory_order_release);
        button_bounds = {};
        observed_contacts.clear();
        button_contacts.clear();
        suppressed_button_contacts.clear();
        toggle_pending = false;
    }

    bool enabled() {
        return enabled_state.load(std::memory_order_acquire);
    }

    Mode current_mode() {
        return current_mode_state.load(std::memory_order_acquire);
    }

    void toggle() {
        if (!enabled()) {
            return;
        }

        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points, true);

        std::lock_guard<std::mutex> lock(state_mutex);

        // wait for every finger to lift so a contact cannot change ownership
        // between the game and piano halfway through its lifetime
        if (!touch_points.empty()) {
            toggle_pending = true;
            return;
        }

        toggle_pending = false;
        const auto next_mode = current_mode() == Mode::Touch ? Mode::Piano : Mode::Touch;
        current_mode_state.store(next_mode, std::memory_order_release);
    }

    void publish_button_bounds(HWND window, const RECT &client_bounds) {
        ButtonBounds next {};
        RECT client_rect {};
        if (window != nullptr && GetClientRect(window, &client_rect) &&
            client_rect.right > 0 && client_rect.bottom > 0) {
            next.window = window;
            next.client = client_bounds;
            next.client_width = client_rect.right;
            next.client_height = client_rect.bottom;
            next.valid = true;
        }

        std::lock_guard<std::mutex> lock(state_mutex);
        button_bounds = next;
    }

    bool consume_mode_button_activation() {
        if (!enabled()) {
            return false;
        }

        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points, true);

        std::lock_guard<std::mutex> lock(state_mutex);
        if (!button_bounds.valid) {
            observed_contacts.clear();
            return false;
        }

        bool pressed = false;
        std::unordered_set<DWORD> active_contacts;
        for (const auto &point : touch_points) {
            active_contacts.insert(point.id);
            POINT position { point.x, point.y };
            if (!observed_contacts.contains(point.id) &&
                PtInRect(&button_bounds.client, position)) {
                button_contacts.insert(point.id);
            }
        }

        // a button touch toggles only after release; this also lets every active
        // contact drain before applying a deferred mode switch
        for (auto it = button_contacts.begin(); it != button_contacts.end();) {
            if (!active_contacts.contains(*it)) {
                it = button_contacts.erase(it);
                toggle_pending = true;
            } else {
                ++it;
            }
        }
        observed_contacts = std::move(active_contacts);
        if (toggle_pending && observed_contacts.empty()) {
            toggle_pending = false;
            pressed = true;
        }
        return pressed;
    }

    uint32_t piano_key_state() {
        if (!enabled() || current_mode() != Mode::Piano) {
            return 0;
        }

        std::vector<TouchPoint> available_touch_points;
        touch_get_points(available_touch_points);

        std::lock_guard<std::mutex> lock(state_mutex);
        if (current_mode() != Mode::Piano || !button_bounds.valid) {
            return 0;
        }

        uint32_t state = 0;
        for (const auto &point : available_touch_points) {
            POINT position { point.x, point.y };
            if (position.x < 0 || position.x >= button_bounds.client_width ||
                position.y < 0 || position.y >= button_bounds.client_height ||
                PtInRect(&button_bounds.client, position)) {
                continue;
            }

            // divide the full client width into 28 equal vertical key slices
            const auto key = static_cast<uint32_t>(
                position.x * 28 / button_bounds.client_width);
            state |= UINT32_C(1) << key;
        }

        return state;
    }

    bool filter_native_touch(const TOUCHINPUT &point, bool synthetic) {
        if (!enabled() || point.dwFlags == 0) {
            return false;
        }

        publish_native_touch(point, synthetic);

        bool suppress_button_contact = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex);

            // contacts beginning on the mode button remain overlay-owned through
            // up, even if the finger later moves outside the button rectangle
            if ((point.dwFlags & TOUCHEVENTF_DOWN) &&
                native_touch_in_button(point, synthetic)) {
                suppressed_button_contacts.insert(point.dwID);
            }
            suppress_button_contact = suppressed_button_contacts.contains(point.dwID);
            if (point.dwFlags & TOUCHEVENTF_UP) {
                suppressed_button_contacts.erase(point.dwID);
            }
        }

        return suppress_button_contact || current_mode() == Mode::Piano;
    }
}