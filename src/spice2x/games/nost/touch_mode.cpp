#include "touch_mode.h"

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "touch/native/nativetouchhook.h"
#include "touch/touch.h"
#include "util/logging.h"

namespace games::nost::touch_mode {

    // hardware events follow two paths: mirrored client-space points feed piano input,
    // while native events reach the game in touch mode and are suppressed in piano mode.
    // mode-button contacts are always suppressed and change that routing after release.
    static constexpr LONG PIANO_LEFT_GAP = 11;
    static constexpr LONG PIANO_RIGHT_GAP = 10;
    static constexpr uint32_t PIANO_KEY_COUNT = 28;

    struct TouchGeometry {
        HWND window = nullptr;
        RECT mode_button {};
        LONG client_width = 0;
        LONG client_height = 0;

        bool valid() const {
            return window != nullptr && client_width > 0 && client_height > 0;
        }
    };

    static std::atomic_bool enabled_state { false };
    static std::atomic<Mode> current_mode_state { Mode::Touch };
    static std::mutex state_mutex;
    static TouchGeometry touch_geometry;

    // contacts that begin on the mode button remain overlay-owned until their up event
    static std::unordered_set<DWORD> suppressed_button_contacts;

    // mode changes wait for this set to empty so a contact cannot begin in one mode
    // and have its move or up event routed according to the other mode
    static std::unordered_set<DWORD> active_hardware_contacts;

    // a hardware button release requests one change after all contacts are released
    static bool mode_change_pending = false;

    static void reset_state_locked() {
        current_mode_state.store(Mode::Touch, std::memory_order_release);
        touch_geometry = {};
        suppressed_button_contacts.clear();
        active_hardware_contacts.clear();
        mode_change_pending = false;
    }

    // mirror PAN's hardware contacts into the shared client-space touch list so
    // the overlay button and piano input consume the same contact lifecycle
    static void publish_native_touch(const nativetouch::NativeTouchEvent &event) {
        // synthetic contacts originate from the shared list and need no mirror
        if (event.synthetic) {
            return;
        }

        if (event.up) {
            std::vector<DWORD> ids { event.id };
            touch_remove_points(&ids);
            return;
        }
        if (!event.down && !event.move) {
            return;
        }

        HWND window = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            window = touch_geometry.window;
        }
        if (window == nullptr) {
            return;
        }

        POINT position { event.x, event.y };
        if (!ScreenToClient(window, &position)) {
            return;
        }

        TouchPoint touch_point {};
        touch_point.id = event.id;
        touch_point.x = position.x;
        touch_point.y = position.y;
        std::vector<TouchPoint> touch_points { touch_point };
        touch_write_points(&touch_points);
    }

    // hardware contacts arrive in screen coordinates, while synthetic contacts
    // have already been transformed into game-client coordinates
    static bool native_touch_in_button(const nativetouch::NativeTouchEvent &event) {
        if (!touch_geometry.valid()) {
            return false;
        }

        POINT position { event.x, event.y };
        if (!event.synthetic && !ScreenToClient(touch_geometry.window, &position)) {
            return false;
        }
        return PtInRect(&touch_geometry.mode_button, position) != FALSE;
    }

    static bool update_touch_state(const nativetouch::NativeTouchEvent &event) {
        std::lock_guard<std::mutex> lock(state_mutex);

        // track every hardware contact, including contacts outside the mode button
        if (!event.synthetic && event.down) {
            active_hardware_contacts.insert(event.id);
        }

        // contacts beginning on the mode button remain overlay-owned through
        // up, even if the finger later moves outside the button rectangle
        if (event.down && native_touch_in_button(event)) {
            suppressed_button_contacts.insert(event.id);
        }

        const bool suppress = suppressed_button_contacts.contains(event.id);
        if (event.up) {
            if (!event.synthetic) {
                active_hardware_contacts.erase(event.id);

                // only a hardware contact released from the button requests a change;
                // synthetic button contacts are suppressed but cannot change modes
                if (suppress) {
                    mode_change_pending = true;
                }

                // apply the change on the final hardware up. switching earlier would
                // either hide an up after its down reached the game, or expose an up
                // whose down was suppressed while piano mode was active
                if (mode_change_pending && active_hardware_contacts.empty()) {
                    mode_change_pending = false;
                    const auto next_mode = current_mode() == Mode::Touch
                        ? Mode::Piano
                        : Mode::Touch;
                    current_mode_state.store(next_mode, std::memory_order_release);
                }
            }
            suppressed_button_contacts.erase(event.id);
        }
        return suppress;
    }

    // remove mirrored contacts so disabling cannot leave active points in the shared list
    static void clear_published_contacts() {
        std::vector<TouchPoint> touch_points;
        touch_get_points(touch_points, true);

        std::vector<DWORD> touch_point_ids;
        touch_point_ids.reserve(touch_points.size());
        for (const auto &point : touch_points) {
            touch_point_ids.push_back(point.id);
        }
        touch_remove_points(&touch_point_ids);
    }

    // install the Nostalgia-specific native touch interception
    void enable() {
        if (enabled_state.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            reset_state_locked();
        }

        nativetouch::set_input_filter(filter_native_touch);
        log_info("nost::touch", "enabled");
    }

    void disable() {
        if (!enabled_state.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        nativetouch::set_input_filter(nullptr);
        clear_published_contacts();

        std::lock_guard<std::mutex> lock(state_mutex);
        reset_state_locked();
    }

    bool enabled() {
        return enabled_state.load(std::memory_order_acquire);
    }

    Mode current_mode() {
        return current_mode_state.load(std::memory_order_acquire);
    }

    // publish the rendered overlay button rectangle in game-client pixels
    void publish_button_bounds(HWND window, const RECT &client_bounds) {
        TouchGeometry next {};
        RECT client_rect {};
        if (window != nullptr && GetClientRect(window, &client_rect) &&
            client_rect.right > 0 && client_rect.bottom > 0) {
            next.window = window;
            next.mode_button = client_bounds;
            next.client_width = client_rect.right;
            next.client_height = client_rect.bottom;
        }

        std::lock_guard<std::mutex> lock(state_mutex);
        touch_geometry = next;
    }

    // return the active 28-key piano bitfield for the PANB input update
    uint32_t piano_key_state() {
        if (!enabled() || current_mode() != Mode::Piano) {
            return 0;
        }

        std::vector<TouchPoint> available_touch_points;
        touch_get_points(available_touch_points);

        std::lock_guard<std::mutex> lock(state_mutex);
        if (current_mode() != Mode::Piano || !touch_geometry.valid()) {
            return 0;
        }

        uint32_t state = 0;
        for (const auto &point : available_touch_points) {
            POINT position { point.x, point.y };
            if (position.x < 0 || position.x >= touch_geometry.client_width ||
                position.y < 0 || position.y >= touch_geometry.client_height ||
                PtInRect(&touch_geometry.mode_button, position)) {
                continue;
            }

            // divide the inset width evenly; touches in either side gap clamp to
            // the nearest outer key so the physical screen edges remain playable
            const auto piano_width =
                touch_geometry.client_width - PIANO_LEFT_GAP - PIANO_RIGHT_GAP;
            uint32_t key = 0;
            if (position.x >= touch_geometry.client_width - PIANO_RIGHT_GAP) {
                key = PIANO_KEY_COUNT - 1;
            } else if (position.x >= PIANO_LEFT_GAP && piano_width > 0) {
                key = static_cast<uint32_t>(
                    (position.x - PIANO_LEFT_GAP) * PIANO_KEY_COUNT / piano_width);
            }
            state |= UINT32_C(1) << key;
        }

        return state;
    }

    // mirror hardware contacts and report whether the game should receive this event
    bool filter_native_touch(const nativetouch::NativeTouchEvent &event) {
        if (!enabled()) {
            return false;
        }

        // update_touch_state may switch modes on the final up event. route that event
        // using the mode that owned its down, and remove its mirrored point before
        // making the new mode visible to piano input.
        const bool piano_mode = current_mode() == Mode::Piano;
        publish_native_touch(event);
        return update_touch_state(event) || piano_mode;
    }
}
