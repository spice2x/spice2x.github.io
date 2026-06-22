#include "obs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "external/imgui/imgui.h"

#include "games/io.h"
#include "overlay/overlay.h"
#include "overlay/imgui/extensions.h"

using namespace std::chrono;

// OBS WebSocket protocol/worker thread lives in obs_websocket.cpp; this file
// owns the ImGui control window and the connection lifecycle.

namespace {

    // status text colors
    const ImVec4 COL_GREEN(0.40f, 0.85f, 0.40f, 1.0f);
    const ImVec4 COL_RED(0.90f, 0.30f, 0.30f, 1.0f);
    const ImVec4 COL_YELLOW(0.95f, 0.80f, 0.30f, 1.0f);
    const ImVec4 COL_GREY(0.60f, 0.60f, 0.60f, 1.0f);

    // muted action-button fills (start = green, stop = red, pause = yellow); the
    // hovered/active shades are derived by brightening the base
    const ImVec4 COL_BTN_GREEN(0.20f, 0.45f, 0.24f, 1.0f);
    const ImVec4 COL_BTN_RED(0.52f, 0.20f, 0.20f, 1.0f);
    const ImVec4 COL_BTN_YELLOW(0.52f, 0.42f, 0.16f, 1.0f);

    // an in-flight request lingers for at most this long before the button frees
    // itself, so a dropped state event can never wedge a control permanently
    const int64_t PENDING_TIMEOUT_MS = 5000;

    int64_t now_tick_ms() {
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::string format_duration(int64_t ms) {
        if (ms < 0) {
            ms = 0;
        }
        const int64_t total_seconds = ms / 1000;
        const int64_t hours = total_seconds / 3600;
        const int64_t minutes = (total_seconds % 3600) / 60;
        const int64_t seconds = total_seconds % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
                 static_cast<long long>(hours),
                 static_cast<long long>(minutes),
                 static_cast<long long>(seconds));
        return buf;
    }
}

namespace overlay::windows {

    OBSControl::OBSControl(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "OBS Control";
        this->flags |= ImGuiWindowFlags_AlwaysAutoResize;
        this->init_pos = overlay::apply_scaling_to_vector(120, 120);
        this->toggle_button = games::OverlayButtons::ToggleOBSControl;

        this->worker_running.store(true);
        this->worker_thread = std::thread(&OBSControl::worker_main, this);
    }

    OBSControl::~OBSControl() {
        this->worker_running.store(false);
        if (this->worker_thread.joinable()) {
            // note: if the worker is mid-connect, WebSocket::from_url performs a
            // blocking getaddrinfo/connect that does not observe worker_running,
            // so this join can stall for the OS connect timeout. the default
            // 127.0.0.1 host fails fast (connection refused); only a misconfigured
            // unreachable remote OBS_CONTROL_HOST would delay shutdown here.
            this->worker_thread.join();
        }
    }

    OBSStatus OBSControl::get_status() {
        std::lock_guard<std::mutex> lock(this->status_mutex);
        return this->status;
    }

    int64_t OBSControl::live_duration_ms(int64_t base_ms, int64_t base_tick, bool ticking) {
        if (!ticking) {
            return (std::max<int64_t>)(base_ms, 0);
        }
        const int64_t now =
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        // clamp so a stale base tick / clock hiccup can never yield a negative
        // duration; callers (FPS rows, build_content) format this directly
        return (std::max<int64_t>)(base_ms + (now - base_tick), 0);
    }

    void OBSControl::build_content() {

        const OBSStatus s = this->get_status();

        // label + colored value on a single line
        const auto status_line = [](const char *label, const ImVec4 &col, const char *value) {
            ImGui::Text("%s", label);
            ImGui::SameLine();
            ImGui::TextColored(col, "%s", value);
        };

        if (!s.connected) {
            if (s.disabled) {
                ImGui::TextColored(COL_GREY, "%s", "OBS Control is disabled");
                return;
            }
            if (s.identifying) {
                status_line("OBS WebSocket:", COL_YELLOW, "Connecting...");
            } else {
                status_line("OBS WebSocket:", COL_GREY, "Not connected");
            }
            const std::string url =
                "ws://" + OBS_CONTROL_HOST + ":" + std::to_string(OBS_CONTROL_PORT);
            status_line("Address:", COL_GREY, url.c_str());
            if (!s.connection_error.empty()) {
                ImGui::TextColored(COL_RED, "%s", s.connection_error.c_str());
            }
            return;
        }

        status_line("OBS WebSocket:", COL_GREEN, "Connected");

        // one fixed content width drives the whole panel so it never resizes as
        // the scene name or button labels change; every row is sized to fit it
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float row_w = overlay::apply_scaling(240);

        if (s.current_scene.empty()) {
            status_line("Scene:", COL_GREY, "(unknown)");
        } else {
            ImGui::Text("Scene:");
            ImGui::SameLine();
            // truncate to the remaining row width so "Scene:" + value together
            // never overflow and push the window wider
            const float label_w = ImGui::CalcTextSize("Scene:").x;
            ImGui::PushStyleColor(ImGuiCol_Text, COL_GREY);
            ImGui::TextTruncated(s.current_scene, row_w - label_w - spacing);
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        const int64_t now = now_tick_ms();
        // every button shares one fixed size; two side-by-side fill the row width,
        // single buttons keep that same size rather than stretching to fill
        const ImVec2 btn((row_w - spacing) * 0.5f, 0);

        // has OBS reached the state a pending action was waiting for?
        const auto reached = [&](OBSAction a) {
            switch (a) {
                case OBSAction::StreamStart: return s.streaming;
                case OBSAction::StreamStop: return !s.streaming;
                case OBSAction::RecordStart: return s.recording;
                case OBSAction::RecordStop: return !s.recording;
                case OBSAction::RecordPause: return s.record_paused;
                case OBSAction::RecordResume: return !s.record_paused;
                default: return true;
            }
        };

        // drop a pending action once OBS confirms the new state, or once the
        // safety deadline lapses (so a dropped event can't wedge the button)
        const auto settle = [&](OBSAction &slot, int64_t deadline) {
            if (slot != OBSAction::None && (reached(slot) || now >= deadline)) {
                slot = OBSAction::None;
            }
        };
        settle(this->stream_pending, this->stream_pending_deadline);
        settle(this->record_pending, this->record_pending_deadline);

        // a colored button that fires a request and marks the output busy on click
        const auto action_button =
            [&](const char *label,
                const ImVec4 &color,
                const char *request,
                OBSAction &slot,
                int64_t &deadline,
                OBSAction action) {

            if (ImGui::ColoredButton(label, color, btn)) {
                enqueue_request(request);
                slot = action;
                deadline = now + PENDING_TIMEOUT_MS;
            }
        };

        // streaming
        {
            const bool pending = this->stream_pending != OBSAction::None;

            if (s.streaming) {
                const int64_t ms = live_duration_ms(
                    s.stream_duration_ms, s.stream_duration_base_tick, true);
                status_line("Streaming:", COL_RED, ("LIVE " + format_duration(ms)).c_str());
            } else {
                status_line("Streaming:", COL_GREY, pending ? "Starting..." : "Idle");
            }

            ImGui::BeginDisabled(pending);
            if (s.streaming) {
                action_button(
                    pending ? "Stopping...##stream" : "Stop Streaming##stream",
                    COL_BTN_RED,
                    "StopStream",
                    this->stream_pending,
                    this->stream_pending_deadline,
                    OBSAction::StreamStop);
            } else {
                action_button(
                    pending ? "Starting...##stream" : "Start Streaming##stream",
                    COL_BTN_GREEN,
                    "StartStream",
                    this->stream_pending,
                    this->stream_pending_deadline,
                    OBSAction::StreamStart);
            }
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        // recording
        {
            const bool pending = this->record_pending != OBSAction::None;

            if (!s.recording) {
                status_line("Recording:", COL_GREY, pending ? "Starting..." : "Idle");
                ImGui::BeginDisabled(pending);
                action_button(
                    pending ? "Starting...##record" : "Start Recording##record",
                    COL_BTN_GREEN,
                    "StartRecord",
                    this->record_pending,
                    this->record_pending_deadline,
                    OBSAction::RecordStart);
                ImGui::EndDisabled();
                return;
            }

            const int64_t ms = live_duration_ms(
                s.record_duration_ms, s.record_duration_base_tick, !s.record_paused);
            if (s.record_paused) {
                status_line("Recording:", COL_YELLOW, ("PAUSED " + format_duration(ms)).c_str());
            } else {
                status_line("Recording:", COL_RED, ("REC " + format_duration(ms)).c_str());
            }

            ImGui::BeginDisabled(pending);
            action_button(
                this->record_pending == OBSAction::RecordStop ? "Stopping...##record" : "Stop Recording##record",
                COL_BTN_RED,
                "StopRecord",
                this->record_pending,
                this->record_pending_deadline, OBSAction::RecordStop);

            ImGui::SameLine();
            if (s.record_paused) {
                action_button(
                    this->record_pending == OBSAction::RecordResume ? "Resuming...##record_toggle" : "Resume##record_toggle",
                    COL_BTN_GREEN,
                    "ResumeRecord",
                    this->record_pending,
                    this->record_pending_deadline,
                    OBSAction::RecordResume);

            } else {
                action_button(
                    this->record_pending == OBSAction::RecordPause ? "Pausing...##record_toggle" : "Pause##record_toggle",
                    COL_BTN_YELLOW,
                    "PauseRecord",
                    this->record_pending,
                    this->record_pending_deadline,
                    OBSAction::RecordPause);
            }
            ImGui::EndDisabled();
        }
    }
}
