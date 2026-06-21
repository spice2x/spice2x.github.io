#include "obs.h"

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
            this->worker_thread.join();
        }
    }

    OBSStatus OBSControl::get_status() {
        std::lock_guard<std::mutex> lock(this->status_mutex);
        return this->status;
    }

    int64_t OBSControl::live_duration_ms(int64_t base_ms, int64_t base_tick, bool ticking) {
        if (!ticking) {
            return base_ms;
        }
        const int64_t now =
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        return base_ms + (now - base_tick);
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
                ImGui::TextColored(COL_YELLOW, "%s", "Connecting to OBS WebSocket...");
            } else {
                ImGui::TextColored(COL_GREY, "%s", "Not connected");
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

        if (s.current_scene.empty()) {
            status_line("Scene:", COL_GREY, "(unknown)");
        } else {
            ImGui::Text("Scene:");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_GREY);
            ImGui::TextTruncated(s.current_scene, overlay::apply_scaling(220));
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        // streaming
        if (s.streaming) {
            const int64_t ms = live_duration_ms(
                s.stream_duration_ms, s.stream_duration_base_tick, true);
            status_line("Streaming:", COL_RED, ("LIVE  " + format_duration(ms)).c_str());
            if (ImGui::Button("Stop Streaming")) {
                enqueue_request("StopStream");
            }
        } else {
            status_line("Streaming:", COL_GREY, "Idle");
            if (ImGui::Button("Start Streaming")) {
                enqueue_request("StartStream");
            }
        }

        ImGui::Separator();

        // recording
        if (!s.recording) {
            status_line("Recording:", COL_GREY, "Idle");
            if (ImGui::Button("Start Recording")) {
                enqueue_request("StartRecord");
            }
            return;
        }

        const int64_t ms = live_duration_ms(
            s.record_duration_ms, s.record_duration_base_tick, !s.record_paused);
        if (s.record_paused) {
            status_line("Recording:", COL_YELLOW, ("PAUSED  " + format_duration(ms)).c_str());
        } else {
            status_line("Recording:", COL_RED, ("REC  " + format_duration(ms)).c_str());
        }
        if (ImGui::Button("Stop Recording")) {
            enqueue_request("StopRecord");
        }
        ImGui::SameLine();
        if (s.record_paused) {
            if (ImGui::Button("Resume")) {
                enqueue_request("ResumeRecord");
            }
        } else {
            if (ImGui::Button("Pause")) {
                enqueue_request("PauseRecord");
            }
        }
    }
}
