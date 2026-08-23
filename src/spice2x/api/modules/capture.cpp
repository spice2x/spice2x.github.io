#include "capture.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include "api/capture_pump.h"
#include "api/stream_format.h"
#include "api/stream_server.h"
#include "external/rapidjson/document.h"
#include "hooks/graphics/graphics.h"
#include "hooks/graphics/jpeg_encoder.h"
#include "util/crypt.h"

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    std::optional<uint32_t> CAPTURE_QUALITY;
    std::optional<uint32_t> CAPTURE_DIVIDE;

    static thread_local std::vector<uint8_t> CAPTURE_BUFFER;

    struct CachedFrame {
        std::vector<uint8_t> jpeg;
        uint64_t timestamp = 0;
        int width = 0;
        int height = 0;
    };

    static std::mutex FRAME_CACHE_M;
    static std::unordered_map<int, CachedFrame> FRAME_CACHE;

    static void add_jpeg_response(
            int screen,
            uint64_t timestamp,
            int width,
            int height,
            const std::vector<uint8_t> &jpeg,
            Response &res) {

        auto encoded = crypt::base64_encode(jpeg.data(), jpeg.size());

        Value data;
        data.SetString(encoded.c_str(), encoded.length(), res.doc()->GetAllocator());
        res.add_data(timestamp);
        res.add_data(width);
        res.add_data(height);
        res.add_data(data);

        std::lock_guard<std::mutex> lock(FRAME_CACHE_M);
        FRAME_CACHE[screen] = {jpeg, timestamp, width, height};
    }

    static bool try_cached_response(int screen, Response &res) {
        std::lock_guard<std::mutex> lock(FRAME_CACHE_M);
        const auto pos = FRAME_CACHE.find(screen);
        if (pos == FRAME_CACHE.end() || pos->second.jpeg.empty()) {
            return false;
        }

        const auto &cached = pos->second;
        auto encoded = crypt::base64_encode(cached.jpeg.data(), cached.jpeg.size());

        Value data;
        data.SetString(encoded.c_str(), encoded.length(), res.doc()->GetAllocator());
        res.add_data(cached.timestamp);
        res.add_data(cached.width);
        res.add_data(cached.height);
        res.add_data(data);
        return true;
    }

    Capture::Capture() : Module("capture") {
        functions["get_screens"] = std::bind(&Capture::get_screens, this, _1, _2);
        functions["get_jpg"] = std::bind(&Capture::get_jpg, this, _1, _2);
        functions["get_streams"] = std::bind(&Capture::get_streams, this, _1, _2);
    }

    /**
     * get_screens()
     */
    void Capture::get_screens(Request &req, Response &res) {

        // aquire screens
        std::vector<int> screens;
        graphics_screens_get(screens);

        // add screens to response
        for (auto &screen : screens) {
            res.add_data(screen);
        }
    }

    /**
     * get_jpg([screen=0, quality=70, downscale=0, divide=1])
     * screen: uint specifying the window
     * quality: uint in range [0, 100]
     * reduce: uint for dividing image size
     */
    void Capture::get_jpg(Request &req, Response &res) {

        // settings
        int screen = 0;
        int quality = 70;
        int divide = 1;
        if (req.params.Size() > 0 && req.params[0].IsUint()) {
            screen = req.params[0].GetUint();
        }

        if (CAPTURE_QUALITY.has_value()) {
            quality = CAPTURE_QUALITY.value();
        } else if (req.params.Size() > 1 && req.params[1].IsUint()) {
            quality = req.params[1].GetUint();
        }

        if (CAPTURE_DIVIDE.has_value()) {
            divide = CAPTURE_DIVIDE.value();
        } else if (req.params.Size() > 2 && req.params[2].IsUint()) {
            divide = req.params[2].GetUint();
        }

        // receive JPEG data
        uint64_t timestamp = 0;
        int width = 0;
        int height = 0;

        std::shared_ptr<uint8_t[]> pixels;
        bool success = capture_pump::capture_direct(
                screen, pixels, divide, &timestamp, &width, &height);

        if (success) {
            CAPTURE_BUFFER.clear();
            success = jpeg_encoder::encode(
                    CAPTURE_BUFFER, pixels.get(), width, height, quality);
        }

        if (success) {
            add_jpeg_response(screen, timestamp, width, height, CAPTURE_BUFFER, res);
            CAPTURE_BUFFER.clear();
            return;
        }

        // fall back to the last successful frame while the game is busy loading
        CAPTURE_BUFFER.clear();
        try_cached_response(screen, res);
    }

    /**
     * get_streams()
     */
    void Capture::get_streams(Request &req, Response &res) {

        auto &alloc = res.doc()->GetAllocator();

        // nothing is listening without -apistream, so there is no stream to describe
        const unsigned short port = stream_server_port();
        if (port == 0) {
            return;
        }

        Value formats(kArrayType);
        for (const auto &[name, path] : stream_formats()) {
            Value entry(kObjectType);
            entry.AddMember("name", Value(name.c_str(), alloc), alloc);
            entry.AddMember("path", Value(path.c_str(), alloc), alloc);
            formats.PushBack(entry, alloc);
        }

        std::vector<int> screen_numbers;
        graphics_screens_get(screen_numbers);

        // measuring a screen nobody has captured yet waits for the game to present, which can
        // take as long as the whole request is allowed, so only one screen is measured per
        // call and the rest are reported null until a later one settles them. which screen
        // gets the attempt rotates, otherwise one that never presents would take every
        // request and the screens behind it would stay unmeasured forever
        int probe_screen = -1;
        {
            std::vector<int> unmeasured;
            for (const auto screen : screen_numbers) {
                if (screen < static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO)
                        && !graphics_capture_last_size(screen, nullptr, nullptr)
                        && !capture_pump::screen_claimed(screen)) {
                    unmeasured.push_back(screen);
                }
            }

            if (!unmeasured.empty()) {
                static std::atomic<unsigned> probe_cursor { 0 };
                probe_screen = unmeasured[probe_cursor.fetch_add(1) % unmeasured.size()];
            }
        }

        Value screens(kArrayType);
        for (const auto screen : screen_numbers) {
            if (screen >= static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO)) {
                continue;
            }

            int width = 0;
            int height = 0;
            bool known = graphics_capture_last_size(screen, &width, &height);

            // a probe holds the screen for as long as it waits, so a second caller arriving
            // during one would queue behind it and then take a wait of its own; let it report
            // the screen as unmeasured instead and pick the size up once the first is done
            static std::atomic<bool> probe_running { false };
            if (!known && screen == probe_screen && !probe_running.exchange(true)) {
                std::shared_ptr<uint8_t[]> pixels;
                known = capture_pump::capture_direct(
                        screen, pixels, 1, nullptr, &width, &height);
                probe_running = false;
            }

            // a screen of unknown size cannot be described, and a client told about it could
            // not size its decoder anyway; leaving it out until it has been measured beats
            // handing over an entry that has to be treated as absent
            if (!known) {
                continue;
            }

            Value entry(kObjectType);
            entry.AddMember("screen", screen, alloc);
            entry.AddMember("width", width, alloc);
            entry.AddMember("height", height, alloc);

            // a screen carries one viewer at a time, so this is what decides whether a client
            // can connect at all; still racy by the time it does, only more honest than not
            entry.AddMember("busy", capture_pump::screen_claimed(screen), alloc);

            screens.PushBack(entry, alloc);
        }

        Value info(kObjectType);
        info.AddMember("port", port, alloc);
        info.AddMember("formats", formats, alloc);
        info.AddMember("screens", screens, alloc);

        res.add_data(info);
    }
}
