#include "notifications.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_internal.h"
#include "external/fmt/include/fmt/format.h"

#include "overlay/overlay.h"
#include "util/time.h"

namespace overlay::notifications {

    bool ENABLED = true;
    Position POSITION = Position::BottomRight;

    struct Notification {
        uint64_t id;
        std::string text;
        Severity severity;
        double created_ms;
        float duration_s;
    };

    static std::mutex g_mutex;
    static std::deque<Notification> g_items;
    static std::atomic<uint64_t> g_next_id { 1 };
    static std::atomic<size_t> g_count { 0 };

    // duration in seconds each notification stays visible
    static constexpr float DURATION_S = 3.0f;

    // maximum number of notifications kept in the queue (oldest dropped beyond this)
    static constexpr size_t MAX_NOTIFICATIONS = 6;

    // time (ms) over which a toast fades out at the end of its lifetime
    static constexpr float FADE_OUT_MS = 400.0f;

    // fixed width of each toast window, in unscaled pixels
    static constexpr float TOAST_WIDTH = 320.0f;

    // gap between the toast stack and the screen edges (right + bottom)
    static constexpr float TOAST_MARGIN = 20.0f;

    // vertical gap between stacked toasts
    static constexpr float TOAST_SPACING = 8.0f;

    // inner padding inside a toast window (horizontal / vertical)
    static constexpr float TOAST_PAD_X = 10.0f;
    static constexpr float TOAST_PAD_Y = 8.0f;

    // width of the colored severity accent bar drawn on the left edge
    static constexpr float TOAST_ACCENT_W = 6.0f;

    // base opacity of the toast background (0..1), multiplied by the fade alpha
    static constexpr float TOAST_BG_ALPHA = 0.85f;

    static constexpr ImGuiWindowFlags TOAST_FLAGS =
          ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_AlwaysAutoResize;

    static ImU32 severity_accent(Severity sev) {
        switch (sev) {
            case Severity::Success: return IM_COL32(80, 200, 120, 255);
            case Severity::Warning: return IM_COL32(230, 180, 60, 255);
            case Severity::Error:   return IM_COL32(220, 60, 60, 255);
            case Severity::Info:
            default:                return IM_COL32(90, 160, 230, 255);
        }
    }

    static bool is_expired(const Notification &n, double now_ms) {
        return (now_ms - n.created_ms) >= (n.duration_s * 1000.0);
    }

    // returns 0.0 .. 1.0 fade alpha based on time remaining
    static float compute_alpha(const Notification &n, double now_ms) {
        const double remaining_ms = (n.duration_s * 1000.0) - (now_ms - n.created_ms);
        if (remaining_ms >= FADE_OUT_MS) {
            return 1.0f;
        }
        if (remaining_ms <= 0.0) {
            return 0.0f;
        }
        return static_cast<float>(remaining_ms / FADE_OUT_MS);
    }

    // drop expired items and copy the rest under a single lock acquisition
    static std::vector<Notification> snapshot_and_prune(double now_ms) {
        std::vector<Notification> snapshot;
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_items.begin(); it != g_items.end();) {
            if (is_expired(*it, now_ms)) {
                it = g_items.erase(it);
            } else {
                ++it;
            }
        }
        g_count.store(g_items.size(), std::memory_order_release);
        snapshot.assign(g_items.begin(), g_items.end());
        return snapshot;
    }

    // is the configured anchor on the right edge of the screen?
    static bool position_is_right(Position p) {
        return p == Position::BottomRight || p == Position::TopRight;
    }

    // is the configured anchor on the bottom edge of the screen?
    static bool position_is_bottom(Position p) {
        return p == Position::BottomRight || p == Position::BottomLeft;
    }

    // draw a single toast anchored to the configured corner; `cursor_y` is the
    // y-coordinate of the toast edge nearest the anchor (top edge for Top* anchors,
    // bottom edge for Bottom* anchors). returns its height in pixels.
    static float draw_toast(const Notification &n, float cursor_y, float alpha) {
        const float toast_width = apply_scaling(TOAST_WIDTH);
        const float margin = apply_scaling(TOAST_MARGIN);
        const ImVec2 &display = ImGui::GetIO().DisplaySize;
        const Position pos = POSITION;

        const auto window_id = fmt::format("##spice_notif_{}", n.id);

        // anchor x/pivot.x select the screen edge; pivot.y matches cursor_y semantics
        const float anchor_x = position_is_right(pos) ? (display.x - margin) : margin;
        const float pivot_x = position_is_right(pos) ? 1.0f : 0.0f;
        const float pivot_y = position_is_bottom(pos) ? 1.0f : 0.0f;
        ImGui::SetNextWindowPos(ImVec2(anchor_x, cursor_y),
                                ImGuiCond_Always, ImVec2(pivot_x, pivot_y));
        ImGui::SetNextWindowSize(ImVec2(toast_width, 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(TOAST_BG_ALPHA * alpha);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(apply_scaling(TOAST_PAD_X), apply_scaling(TOAST_PAD_Y)));

        float height = 0.f;
        if (ImGui::Begin(window_id.c_str(), nullptr, TOAST_FLAGS)) {
            const ImVec2 win_pos = ImGui::GetWindowPos();
            const ImVec2 win_size = ImGui::GetWindowSize();

            // accent bar on the left edge of the window
            const ImU32 accent = severity_accent(n.severity);
            const ImU32 accent_faded =
                (accent & 0x00FFFFFFu) | (static_cast<ImU32>(alpha * 255.0f) << 24);
            ImGui::GetWindowDrawList()->AddRectFilled(
                win_pos,
                ImVec2(win_pos.x + apply_scaling(TOAST_ACCENT_W), win_pos.y + win_size.y),
                accent_faded);

            // small gutter past the accent bar, then wrapped text
            ImGui::Dummy(ImVec2(apply_scaling(2.0f), 0.f));
            ImGui::SameLine();
            ImGui::PushTextWrapPos(win_pos.x + win_size.x - apply_scaling(TOAST_PAD_X));
            ImGui::TextUnformatted(n.text.c_str());
            ImGui::PopTextWrapPos();

            height = ImGui::GetWindowSize().y;
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        return height;
    }

    uint64_t add(Severity severity, std::string text) {
        if (!ENABLED || !overlay::ENABLED || overlay::OVERLAY == nullptr) {
            return 0;
        }

        Notification n {
            .id = g_next_id.fetch_add(1, std::memory_order_relaxed),
            .text = std::move(text),
            .severity = severity,
            .created_ms = get_performance_milliseconds(),
            .duration_s = DURATION_S,
        };

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_items.push_back(std::move(n));
            while (g_items.size() > MAX_NOTIFICATIONS) {
                g_items.pop_front();
            }
            g_count.store(g_items.size(), std::memory_order_release);
        }
        return n.id;
    }

    uint64_t add_throttled(Severity severity, const std::string &key,
                           double cooldown_seconds, std::string text) {
        if (!ENABLED || !overlay::ENABLED || overlay::OVERLAY == nullptr) {
            return 0;
        }
        // per-key last-emit timestamps live behind their own lock so we don't
        // hold g_mutex across the map lookup.
        static std::mutex throttle_mutex;
        static std::unordered_map<std::string, double> last_emit_ms;
        const double now_ms = get_performance_milliseconds();
        {
            std::lock_guard<std::mutex> lock(throttle_mutex);
            auto it = last_emit_ms.find(key);
            if (it != last_emit_ms.end()
                && (now_ms - it->second) < (cooldown_seconds * 1000.0)) {
                return 0;
            }
            last_emit_ms[key] = now_ms;
        }
        return add(severity, std::move(text));
    }

    bool has_pending() {
        return g_count.load(std::memory_order_acquire) > 0;
    }

    void draw() {
        const double now_ms = get_performance_milliseconds();
        const auto snapshot = snapshot_and_prune(now_ms);
        if (snapshot.empty()) {
            return;
        }

        // stack from the anchored edge with newest toast at the anchor.
        // Bottom* anchors stack upward; Top* anchors stack downward.
        const float spacing = apply_scaling(TOAST_SPACING);
        const float margin = apply_scaling(TOAST_MARGIN);
        const bool bottom = position_is_bottom(POSITION);
        float cursor_y = bottom
            ? (ImGui::GetIO().DisplaySize.y - margin)
            : margin;
        for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
            const float alpha = compute_alpha(*it, now_ms);
            const float height = draw_toast(*it, cursor_y, alpha);
            cursor_y += bottom ? -(height + spacing) : (height + spacing);
        }
    }

    void apply_game_default_position(const std::string &game_name) {
        if (game_name == "Reflec Beat") {
            POSITION = Position::TopRight;
        }
        // others keep the default (BottomRight)
    }
}
