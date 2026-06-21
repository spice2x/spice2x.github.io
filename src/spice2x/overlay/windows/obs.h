#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "external/rapidjson/fwd.h"
#include "overlay/window.h"

namespace easywsclient {
    class WebSocket;
}

namespace overlay::windows {

    // OBS WebSocket connection settings, resolved once at launch from the merged
    // launcher options (command line + saved config) following the same pattern
    // as the other global launch settings in launcher.cpp
    extern bool OBS_CONTROL_ENABLED;
    extern std::string OBS_CONTROL_HOST;
    extern uint16_t OBS_CONTROL_PORT;
    extern std::string OBS_CONTROL_PASSWORD;

    // when true, easywsclient's internal diagnostics are routed to the logger
    extern bool OBS_CONTROL_DEBUG;

    // status snapshot shared between the OBS worker thread and the render thread
    struct OBSStatus {
        bool disabled = true;
        bool connected = false;
        bool identifying = false;
        std::string connection_error;

        // name of the active program scene (read-only, from obs-websocket)
        std::string current_scene;

        bool streaming = false;
        bool recording = false;
        bool record_paused = false;

        // duration base values (milliseconds) and the local timestamp (ms since
        // steady epoch) at which they were last refreshed, so the UI can tick a
        // smooth timer between polls
        int64_t stream_duration_ms = 0;
        int64_t record_duration_ms = 0;
        int64_t stream_duration_base_tick = 0;
        int64_t record_duration_base_tick = 0;
    };

    class OBSControl : public Window {
    public:
        OBSControl(SpiceOverlay *overlay);
        ~OBSControl() override;

        void build_content() override;

        // thread-safe snapshot of the current status for external widgets (e.g. FPS)
        OBSStatus get_status();

        // live (ticked) duration in ms from a base value/tick captured at last poll
        static int64_t live_duration_ms(int64_t base_ms, int64_t base_tick, bool ticking);

    private:
        // worker thread entry + helpers (implementation owns the WebSocket)
        void worker_main();

        // run one connected session loop until the socket closes or we stop
        void run_session(easywsclient::WebSocket *ws, const std::string &password,
                         uint64_t &request_id);

        // handle a single inbound obs-websocket message (parses + dispatches)
        void handle_message(easywsclient::WebSocket *ws, const std::string &message,
                            const std::string &password, uint64_t &request_id,
                            bool &identified);

        // per-opcode handlers dispatched from handle_message
        using request_fn = std::function<void(const char *request_type)>;
        void handle_identified(bool &identified, const request_fn &request);
        void handle_event(const rapidjson::Value &d, const request_fn &request);
        void handle_response(const rapidjson::Value &d);

        void enqueue_request(const std::string &request_type);

        // sleep up to total_ms, waking early if the worker is asked to stop
        void interruptible_sleep(int total_ms);

        // worker thread
        std::thread worker_thread;
        std::atomic<bool> worker_running { false };

        // shared status (guarded by status_mutex)
        std::mutex status_mutex;
        OBSStatus status;

        // outgoing user commands (guarded by command_mutex)
        std::mutex command_mutex;
        std::deque<std::string> command_queue;
    };
}
