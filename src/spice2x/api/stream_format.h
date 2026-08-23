#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "capture_pump.h"

namespace api {

    // writes bytes to the client; false once the connection is gone
    using StreamSend = std::function<bool(const void *, size_t)>;

    // one wire format, instantiated per connection so it can keep encoder state across frames
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

    // null when the path does not name a format this build supports
    std::unique_ptr<StreamWriter> make_stream_writer(
            const std::string &path, int quality, int fps);

    // name and path of every format compiled into this build, for clients to pick from
    std::vector<std::pair<std::string, std::string>> stream_formats();
}
