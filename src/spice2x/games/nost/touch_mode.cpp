#include "touch_mode.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "touch/native/nativetouchhook.h"
#include "util/logging.h"

namespace games::nost::touch_mode {

    // native contact positions feed piano input directly. native events reach the game
    // in nav mode and are suppressed in piano mode. mode-button contacts are always
    // suppressed and change that routing after release.
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

    struct NativeContact {
        POINT position {};

        // position contains game-client coordinates
        bool client_position_valid = false;

        // down began on the mode switch button; remains true through up
        bool mode_button = false;
    };

    static std::atomic_bool accept_events { false };
    static std::atomic<Mode> current_mode_state { Mode::Nav };
    static std::mutex state_mutex;
    static TouchGeometry touch_geometry;

    // native contacts are kept by ID so each contact contributes exactly one position
    static std::unordered_map<DWORD, NativeContact> active_contacts;

    // a hardware button release requests one change after all contacts are released
    static bool mode_change_pending = false;

    static void reset_state_locked() {
        current_mode_state.store(Mode::Nav, std::memory_order_release);
        touch_geometry = {};
        active_contacts.clear();
        mode_change_pending = false;
    }

    // hardware contacts arrive in screen coordinates
    static bool native_touch_in_button(const nativetouch::NativeTouchEvent &event) {
        if (!touch_geometry.valid()) {
            return false;
        }

        POINT position { event.x, event.y };
        if (!ScreenToClient(touch_geometry.window, &position)) {
            return false;
        }
        return PtInRect(&touch_geometry.mode_button, position) != FALSE;
    }

    static bool update_touch_state(const nativetouch::NativeTouchEvent &event) {
        std::lock_guard<std::mutex> lock(state_mutex);

        // first, process down / move events
        if (event.down || event.move) {
            auto contact = active_contacts.try_emplace(event.id).first;

            // keep track of IDs that began as a down on the mode switch button
            if (event.down) {
                contact->second.mode_button = native_touch_in_button(event);
            }

            // check for valid position
            POINT position { event.x, event.y };
            if (touch_geometry.window != nullptr &&
                ScreenToClient(touch_geometry.window, &position)) {
                contact->second.position = position;
                contact->second.client_position_valid = true;
            }
        }

        const auto contact = active_contacts.find(event.id);
        const bool mode_button_contact = contact != active_contacts.end() &&
            contact->second.mode_button;

        // process up events
        if (event.up) {
            active_contacts.erase(event.id);

            // if a contact that began down event on the mode switch button has
            // been released, a mode switch is now pending
            if (mode_button_contact) {
                mode_change_pending = true;
            }

            // apply the change on the final hardware up. switching earlier would
            // split another contact's down and up events across different modes
            if (mode_change_pending && active_contacts.empty()) {
                mode_change_pending = false;
                const auto next_mode = current_mode() == Mode::Nav ? Mode::Piano : Mode::Nav;
                current_mode_state.store(next_mode, std::memory_order_release);
            }
        }
        return mode_button_contact;
    }

    // install the Nostalgia-specific native touch interception
    void enable() {
        if (accept_events.exchange(true, std::memory_order_acq_rel)) {
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
        if (!accept_events.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        nativetouch::set_input_filter(nullptr);

        std::lock_guard<std::mutex> lock(state_mutex);
        reset_state_locked();
    }

    bool enabled() {
        return accept_events.load(std::memory_order_acquire);
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

        std::lock_guard<std::mutex> lock(state_mutex);
        if (current_mode() != Mode::Piano || !touch_geometry.valid()) {
            return 0;
        }

        uint32_t state = 0;
        for (const auto &contact : active_contacts) {
            // invalid position or mode-button contact; ignore these contacts
            if (!contact.second.client_position_valid || contact.second.mode_button) {
                continue;
            }

            const auto &position = contact.second.position;

            // outside the client area or on the mode button; ignore these contacts
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

    // update native contacts and report whether this event should be hidden from the game
    bool filter_native_touch(const nativetouch::NativeTouchEvent &event) {
        // synthetic events are outside this hardware-only feature
        if (!enabled() || event.synthetic) {
            // false leaves the event visible to the game
            return false;
        }

        // snapshot routing before an up event can commit a pending mode switch
        const bool piano_mode_before_update = current_mode() == Mode::Piano;

        // update the contact lifetime and commit any pending switch when safe
        const bool mode_button_contact = update_touch_state(event);

        // hide every event in a contact that began on the mode switch button
        if (mode_button_contact) {
            return true;
        }

        // piano mode consumes hardware events; nav mode forwards them to the game
        return piano_mode_before_update;
    }
}
