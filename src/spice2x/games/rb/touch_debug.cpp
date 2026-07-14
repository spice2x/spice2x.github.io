#include "touch_debug.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

#include "external/imgui/imgui.h"
#include "games/rb/rb.h"
#include "games/rb/touch_defs.h"

namespace games::rb {

    struct TouchDebugState {
        std::array<unsigned char, TOUCH_PACKET_SIZE> packet {};
        bool is_landscape = false;
    };

    std::atomic_bool TOUCH_DEBUG_OVERLAY = false;
    static std::atomic_bool TOUCH_ACTIVE = false;
    static std::mutex TOUCH_DEBUG_STATE_M;
    static TouchDebugState TOUCH_DEBUG_STATE;

    static float touch_scale_factor() {
        return TOUCH_SCALING / (float) TOUCH_SCALE_DEFAULT;
    }

    static void clear_touch_debug_state() {
        std::lock_guard<std::mutex> lock(TOUCH_DEBUG_STATE_M);
        TOUCH_DEBUG_STATE = {};
    }

    static TouchDebugState get_touch_debug_state() {
        std::lock_guard<std::mutex> lock(TOUCH_DEBUG_STATE_M);
        return TOUCH_DEBUG_STATE;
    }

    static bool packet_bit_active(
            const std::array<unsigned char, TOUCH_PACKET_SIZE> &packet, int bit) {
        return (packet[TOUCH_PACKET_DATA_OFFSET + bit / 8] & (1u << (bit % 8))) != 0;
    }

    static int sensor_center(int sensor, int sensor_count, int extent) {
        return ((sensor * 2 + 1) * extent) / (sensor_count * 2);
    }

    static float sensor_span_position(int sensor, int sensor_count, int extent) {
        return sensor * (extent - 1) / (float) (sensor_count - 1);
    }

    bool touch_debug_overlay_enabled() {
        return TOUCH_DEBUG_OVERLAY && TOUCH_ACTIVE.load(std::memory_order_acquire);
    }

    void touch_draw_debug_overlay() {
        if (!touch_debug_overlay_enabled()) {
            return;
        }

        const auto &io = ImGui::GetIO();
        int width = static_cast<int>(io.DisplaySize.x);
        int height = static_cast<int>(io.DisplaySize.y);
        if (width <= 0 || height <= 0) {
            return;
        }

        const float scale_factor = touch_scale_factor();
        const float left = width * (1.f - scale_factor) / 2.f;
        const float top = height * (1.f - scale_factor) / 2.f;
        const float right = width - left;
        const float bottom = height - top;

        TouchDebugState state = get_touch_debug_state();
        ImDrawList *draw_list = ImGui::GetBackgroundDrawList();
        auto draw_line = [&](float x1, float y1, float x2, float y2) {
            draw_list->AddLine(
                ImVec2(x1, y1), ImVec2(x2, y2),
                IM_COL32(0, 255, 64, 255), 2.f);
        };

        // show the valid input area when touch scaling restricts it
        if (TOUCH_SCALING != TOUCH_SCALE_DEFAULT) {
            draw_list->AddRect(
                ImVec2(left, top), ImVec2(right, bottom),
                IM_COL32(255, 255, 255, 255), 0.f, 0, 2.f);
        }

        // spread the usable X sensors 2..45 from edge to edge
        for (int sensor = X_SENSOR_FIRST_ACTIVE; sensor <= X_SENSOR_LAST_ACTIVE; sensor++) {
            if (!packet_bit_active(state.packet, X_SENSOR_FIRST_BIT + sensor)) {
                continue;
            }

            float position = sensor_span_position(
                sensor - X_SENSOR_FIRST_ACTIVE, X_SENSOR_ACTIVE_COUNT,
                state.is_landscape ? height : width);
            if (state.is_landscape) {
                float y = top + position * scale_factor;
                draw_line(left, y, right, y);
            } else {
                float x = left + position * scale_factor;
                draw_line(x, top, x, bottom);
            }
        }

        for (int sensor = 0; sensor < Y_SENSOR_COUNT; sensor++) {
            if (!packet_bit_active(state.packet, Y_SENSOR_FIRST_BIT - sensor)) {
                continue;
            }

            int position = sensor_center(
                sensor, Y_SENSOR_COUNT,
                state.is_landscape ? width : height);
            if (state.is_landscape) {
                float x = right - position * scale_factor;
                draw_line(x, top, x, bottom);
            } else {
                float y = top + position * scale_factor;
                draw_line(left, y, right, y);
            }
        }
    }

    void touch_debug_attach() {
        clear_touch_debug_state();
        TOUCH_ACTIVE.store(true, std::memory_order_release);
    }

    void touch_debug_detach() {
        TOUCH_ACTIVE.store(false, std::memory_order_release);
        clear_touch_debug_state();
    }

    void touch_debug_publish(const unsigned char *data, bool is_landscape) {
        if (!TOUCH_DEBUG_OVERLAY) {
            return;
        }

        std::lock_guard<std::mutex> lock(TOUCH_DEBUG_STATE_M);
        memcpy(TOUCH_DEBUG_STATE.packet.data(), data, TOUCH_PACKET_SIZE);
        TOUCH_DEBUG_STATE.is_landscape = is_landscape;
    }
}
