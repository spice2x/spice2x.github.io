#include <winsock2.h>

#include "obs.h"

#include <chrono>

#include "external/easywsclient/easywsclient.hpp"
#include "external/rapidjson/document.h"
#include "external/rapidjson/stringbuffer.h"
#include "external/rapidjson/writer.h"
#include "external/hash-library/sha256.h"

#include "overlay/notifications.h"
#include "util/crypt.h"
#include "util/logging.h"

// defined in easywsclient.cpp; gates its internal diagnostic output
extern bool EASYWSCLIENT_LOGGING_ENABLED;

using easywsclient::WebSocket;
using namespace std::chrono;

// obs-websocket v5 message flow (https://github.com/obsproject/obs-websocket):
//   server -> op 0 Hello            (may include an auth challenge)
//   client -> op 1 Identify         (answers the challenge, picks rpcVersion)
//   server -> op 2 Identified       (handshake done; requests may now be sent)
//   server -> op 5 Event            (state changes: stream/record/scene/...)
//   client -> op 6 Request          (e.g. GetStreamStatus, StartRecord)
//   server -> op 7 RequestResponse  (reply to a Request, carries responseData)
// Every message is { "op": <int>, "d": { ... } }. Event fields are nested under
// d["eventData"] and request replies under d["responseData"], not in d directly.

namespace {

    int64_t now_ms() {
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // raw SHA256 digest -> base64 (obs-websocket v5 auth primitive)
    std::string sha256_base64(const std::string &input) {
        SHA256 hasher;
        hasher.add(input.data(), input.size());
        unsigned char digest[SHA256::HashBytes];
        hasher.getHash(digest);
        return crypt::base64_encode(reinterpret_cast<const uint8_t *>(digest), SHA256::HashBytes);
    }

    // auth = base64(sha256(base64(sha256(password + salt)) + challenge))
    std::string compute_auth(const std::string &password, const std::string &salt,
            const std::string &challenge) {
        const std::string secret = sha256_base64(password + salt);
        return sha256_base64(secret + challenge);
    }

    std::string build_identify(int rpc_version, const std::string &authentication) {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("op"); w.Int(1);
        w.Key("d");
        w.StartObject();
        w.Key("rpcVersion"); w.Int(rpc_version);
        if (!authentication.empty()) {
            w.Key("authentication"); w.String(authentication.c_str());
        }
        w.EndObject();
        w.EndObject();
        return sb.GetString();
    }

    std::string build_request(const std::string &request_type, uint64_t request_id) {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("op"); w.Int(6);
        w.Key("d");
        w.StartObject();
        w.Key("requestType"); w.String(request_type.c_str());
        w.Key("requestId"); w.String(std::to_string(request_id).c_str());
        w.EndObject();
        w.EndObject();
        return sb.GetString();
    }

    // read a numeric field as int64 ms (obs sends durations as integers/doubles)
    int64_t json_number(const rapidjson::Value &obj, const char *key) {
        if (obj.HasMember(key) && obj[key].IsNumber()) {
            return static_cast<int64_t>(obj[key].GetDouble());
        }
        return 0;
    }

    bool json_bool(const rapidjson::Value &obj, const char *key) {
        return obj.HasMember(key) && obj[key].IsBool() && obj[key].GetBool();
    }

    std::string json_string(const rapidjson::Value &obj, const char *key) {
        if (obj.HasMember(key) && obj[key].IsString()) {
            return obj[key].GetString();
        }
        return "";
    }

    // build the Identify (op 1) reply to a Hello (op 0), answering the auth
    // challenge if the server requires one
    std::string build_hello_response(const rapidjson::Value &d, const std::string &password) {
        int rpc_version = 1;
        if (d.HasMember("rpcVersion") && d["rpcVersion"].IsInt()) {
            rpc_version = d["rpcVersion"].GetInt();
        }
        std::string auth;
        if (d.HasMember("authentication") && d["authentication"].IsObject()) {
            const rapidjson::Value &a = d["authentication"];
            const std::string challenge = json_string(a, "challenge");
            const std::string salt = json_string(a, "salt");
            if (!challenge.empty()) {
                auth = compute_auth(password, salt, challenge);
            }
        }
        return build_identify(rpc_version, auth);
    }

    // map an obs-websocket outputState to a user notification. `label` is the
    // output kind ("Streaming" or "Recording"). transitional states are ignored.
    void notify_output_state(const char *label, const std::string &state) {
        using overlay::notifications::Severity;

        struct StateToast {
            const char *state;
            Severity severity;
            const char *verb;
        };
        static const StateToast TOASTS[] = {
            { "OBS_WEBSOCKET_OUTPUT_STARTED", Severity::Success, "started" },
            { "OBS_WEBSOCKET_OUTPUT_STOPPED", Severity::Info, "stopped" },
            { "OBS_WEBSOCKET_OUTPUT_PAUSED", Severity::Warning, "paused" },
            { "OBS_WEBSOCKET_OUTPUT_RESUMED", Severity::Info, "resumed" },
        };

        for (const auto &toast : TOASTS) {
            if (state == toast.state) {
                overlay::notifications::add(toast.severity,
                    "OBS: " + std::string(label) + " " + toast.verb);
                return;
            }
        }
    }
}

namespace overlay::windows {

    // connection settings resolved at launch (see launcher.cpp)
    bool OBS_CONTROL_ENABLED = false;
    std::string OBS_CONTROL_HOST = "127.0.0.1";
    uint16_t OBS_CONTROL_PORT = 4455;
    std::string OBS_CONTROL_PASSWORD;
    bool OBS_CONTROL_DEBUG = false;

    void OBSControl::enqueue_request(const std::string &request_type) {
        std::lock_guard<std::mutex> lock(this->command_mutex);
        this->command_queue.push_back(request_type);
    }

    void OBSControl::interruptible_sleep(int total_ms) {
        for (int elapsed = 0; elapsed < total_ms && this->worker_running.load(); elapsed += 100) {
            std::this_thread::sleep_for(milliseconds(100));
        }
    }

    void OBSControl::handle_message(WebSocket *ws, const std::string &message,
            const std::string &password, uint64_t &request_id, bool &identified) {

        rapidjson::Document doc;
        if (doc.Parse(message.c_str()).HasParseError() || !doc.IsObject()) {
            return;
        }
        if (!doc.HasMember("op") || !doc["op"].IsInt()
                || !doc.HasMember("d") || !doc["d"].IsObject()) {
            return;
        }
        const int op = doc["op"].GetInt();
        const rapidjson::Value &d = doc["d"];

        // send an op 6 Request; each needs a unique id (we never match replies
        // back, so a simple incrementing counter is enough)
        const request_fn request = [&](const char *request_type) {
            ws->send(build_request(request_type, ++request_id));
        };

        switch (op) {
            case 0: // Hello
                // server greeted us: reply with Identify, solving the auth
                // challenge inline if the server set a password
                ws->send(build_hello_response(d, password));
                break;
            case 2: // Identified
                this->handle_identified(identified, request);
                break;
            case 5: // Event
                this->handle_event(d, request);
                break;
            case 7: // RequestResponse
                this->handle_response(d);
                break;
            default:
                break;
        }
    }

    void OBSControl::handle_identified(bool &identified, const request_fn &request) {
        // handshake complete: the connection is now usable for requests
        identified = true;
        {
            std::lock_guard<std::mutex> lock(this->status_mutex);
            this->status.connected = true;
            this->status.identifying = false;
            this->status.connection_error.clear();
        }
        log_info("obs", "connected and identified");

        // pull the current scene/stream/record state so the UI starts accurate
        request("GetCurrentProgramScene");
        request("GetStreamStatus");
        request("GetRecordStatus");
    }

    void OBSControl::handle_event(const rapidjson::Value &d, const request_fn &request) {
        const std::string type = json_string(d, "eventType");
        const bool has_data = d.HasMember("eventData") && d["eventData"].IsObject();

        if (type == "StreamStateChanged") {
            if (has_data) {
                notify_output_state("Streaming", json_string(d["eventData"], "outputState"));
            }
            request("GetStreamStatus");
        } else if (type == "RecordStateChanged") {
            if (has_data) {
                notify_output_state("Recording", json_string(d["eventData"], "outputState"));
            }
            request("GetRecordStatus");
        } else if (type == "CurrentProgramSceneChanged" && has_data) {
            std::lock_guard<std::mutex> lock(this->status_mutex);
            this->status.current_scene = json_string(d["eventData"], "sceneName");
        }
    }

    void OBSControl::handle_response(const rapidjson::Value &d) {
        const std::string type = json_string(d, "requestType");
        if (type.empty() || !d.HasMember("responseData") || !d["responseData"].IsObject()) {
            return;
        }
        const rapidjson::Value &rd = d["responseData"];

        std::lock_guard<std::mutex> lock(this->status_mutex);
        if (type == "GetCurrentProgramScene") {
            // newer obs returns sceneName; older builds used the now-deprecated
            // currentProgramSceneName, so prefer it then fall back
            std::string scene = json_string(rd, "currentProgramSceneName");
            if (scene.empty()) {
                scene = json_string(rd, "sceneName");
            }
            this->status.current_scene = scene;
        } else if (type == "GetStreamStatus") {
            this->status.streaming = json_bool(rd, "outputActive");
            this->status.stream_duration_ms = json_number(rd, "outputDuration");
            this->status.stream_duration_base_tick = now_ms();
        } else if (type == "GetRecordStatus") {
            this->status.recording = json_bool(rd, "outputActive");
            this->status.record_paused = json_bool(rd, "outputPaused");
            this->status.record_duration_ms = json_number(rd, "outputDuration");
            this->status.record_duration_base_tick = now_ms();
        }
    }

    void OBSControl::run_session(WebSocket *ws, const std::string &password, uint64_t &request_id) {

        // one iteration of a live connection: pump socket I/O, dispatch any
        // inbound messages, flush queued user commands, then refresh status
        bool identified = false;
        // handle_identified() issues the first GetStreamStatus/GetRecordStatus on
        // identify, so the periodic poll below just maintains the ~1s cadence
        auto last_status_poll = steady_clock::now();

        // send a request with the next sequential id
        const auto request = [&](const char *request_type) {
            ws->send(build_request(request_type, ++request_id));
        };

        while (this->worker_running.load() && ws->getReadyState() != WebSocket::CLOSED) {
            ws->poll(100);

            ws->dispatch([&](const std::string &message) {
                this->handle_message(ws, message, password, request_id, identified);
            });

            if (ws->getReadyState() == WebSocket::CLOSED) {
                break;
            }

            // nothing may be sent until the op 2 Identified handshake completes
            if (!identified) {
                continue;
            }

            // drain user commands
            std::deque<std::string> pending;
            {
                std::lock_guard<std::mutex> lock(this->command_mutex);
                pending.swap(this->command_queue);
            }
            for (const auto &cmd : pending) {
                ws->send(build_request(cmd, ++request_id));
            }

            // periodic status refresh (~1s) for live duration
            const auto now = steady_clock::now();
            if (now - last_status_poll >= milliseconds(1000)) {
                last_status_poll = now;
                request("GetStreamStatus");
                request("GetRecordStatus");
            }
        }
    }

    void OBSControl::worker_main() {

        // connection settings are resolved once at launch into globals
        // (launcher.cpp, from the merged command-line + saved config options)
        if (!OBS_CONTROL_ENABLED) {
            log_info("obs", "disabled, not connecting");
            std::lock_guard<std::mutex> lock(this->status_mutex);
            this->status.disabled = true;
            return;
        }

        const std::string url = "ws://" + OBS_CONTROL_HOST + ":" + std::to_string(OBS_CONTROL_PORT);
        const std::string password = OBS_CONTROL_PASSWORD;

        // opt easywsclient's internal diagnostics in/out per the debug option
        EASYWSCLIENT_LOGGING_ENABLED = OBS_CONTROL_DEBUG;

        // winsock is reference-counted: the app performs its own WSAStartup at
        // launch (which outlives this worker), so this paired Startup/Cleanup only
        // bumps the refcount and the WSACleanup below never tears down winsock for
        // the rest of the process
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        log_info("obs", "enabled, connecting to {}", url);

        uint64_t request_id = 0;

        // reconnect loop: keep a session alive while enabled, retrying on drop
        while (this->worker_running.load()) {

            // mark "connecting" for the UI before each attempt
            {
                std::lock_guard<std::mutex> lock(this->status_mutex);
                this->status.disabled = false;
                this->status.connected = false;
                this->status.identifying = true;
                this->status.connection_error.clear();
            }

            // open the TCP socket and perform the WebSocket handshake; null means
            // OBS is unreachable (not running / wrong port / obs-websocket off)
            WebSocket::pointer ws = WebSocket::from_url(url);
            if (ws == nullptr) {
                {
                    std::lock_guard<std::mutex> lock(this->status_mutex);
                    this->status.identifying = false;
                    this->status.connection_error = "Unable to connect to " + url;
                }
                interruptible_sleep(3000);
                continue;
            }

            // blocks here pumping the connection until it closes or we stop
            this->run_session(ws, password, request_id);

            // session ended: close the socket cleanly and free it
            ws->close();
            ws->poll();
            delete ws;

            // connection dropped: clear live state so the UI doesn't show stale
            // scene/stream/record info while disconnected
            {
                std::lock_guard<std::mutex> lock(this->status_mutex);
                this->status.connected = false;
                this->status.identifying = false;
                if (this->status.connection_error.empty()) {
                    this->status.connection_error = "Disconnected";
                }
                this->status.streaming = false;
                this->status.recording = false;
                this->status.record_paused = false;
                this->status.current_scene.clear();
            }

            // clear any commands queued while disconnected
            {
                std::lock_guard<std::mutex> lock(this->command_mutex);
                this->command_queue.clear();
            }

            // wait before reconnecting (interruptible)
            interruptible_sleep(2000);
        }

        WSACleanup();
        log_info("obs", "OBS overlay worker stopped");
    }
}
