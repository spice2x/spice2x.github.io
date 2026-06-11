#include "capture.h"
#include <functional>
#include <mutex>
#include <unordered_map>
#include "external/rapidjson/document.h"
#include "hooks/graphics/graphics.h"
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
        CAPTURE_BUFFER.clear();
        CAPTURE_BUFFER.reserve(1024 * 128);

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
        graphics_capture_trigger(screen);
        bool success = graphics_capture_receive_jpeg(screen, [] (uint8_t byte) {
            CAPTURE_BUFFER.push_back(byte);
        }, true, quality, true, divide, &timestamp, &width, &height);

        if (success) {
            add_jpeg_response(screen, timestamp, width, height, CAPTURE_BUFFER, res);
            CAPTURE_BUFFER.clear();
            return;
        }

        // fall back to the last successful frame while the game is busy loading
        CAPTURE_BUFFER.clear();
        try_cached_response(screen, res);
    }
}
