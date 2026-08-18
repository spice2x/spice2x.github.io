#include "stream_format.h"

namespace api {

    namespace {

        constexpr const char *MJPEG_BOUNDARY = "spice2xframe";

        /*
         * multipart/x-mixed-replace: every frame is a standalone JPEG in its own MIME part.
         * No container and no inter-frame state, so a client may join at any point.
         */
        class MjpegWriter : public StreamWriter {
        public:

            std::string content_type() const override {
                return std::string("multipart/x-mixed-replace; boundary=") + MJPEG_BOUNDARY;
            }

            bool write(const StreamSend &send, const capture_pump::Frame &frame) override {
                const std::string part =
                        "--" + std::string(MJPEG_BOUNDARY) + "\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: " + std::to_string(frame.jpeg.size()) + "\r\n"
                        "\r\n";

                return send(part.data(), part.size())
                        && send(frame.jpeg.data(), frame.jpeg.size())
                        && send("\r\n", 2);
            }
        };
    }

    std::unique_ptr<StreamWriter> make_stream_writer(const std::string &path) {

        if (path == "/stream.mjpg") {
            return std::make_unique<MjpegWriter>();
        }

        return nullptr;
    }
}
