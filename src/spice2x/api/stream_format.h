#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "capture_pump.h"

namespace api {

    // writes bytes to the client; false once the connection is gone
    using StreamSend = std::function<bool(const void *, size_t)>;

    /*
     * One wire format for the video stream. Instantiated per connection so a format is free to
     * keep muxer state across frames. Frames arrive already JPEG-encoded by the capture pump;
     * a format wanting a different codec re-encodes inside its own writer.
     */
    class StreamWriter {
    public:
        virtual ~StreamWriter() = default;

        StreamWriter(const StreamWriter &) = delete;
        StreamWriter &operator=(const StreamWriter &) = delete;

        // value for the HTTP Content-Type response header
        virtual std::string content_type() const = 0;

        // for formats that open with an init segment; runs once before any frame
        virtual bool begin(const StreamSend &send) { return true; }

        virtual bool write(const StreamSend &send, const capture_pump::Frame &frame) = 0;

    protected:
        StreamWriter() = default;
    };

    // null when the path does not name a known format
    std::unique_ptr<StreamWriter> make_stream_writer(const std::string &path);
}
