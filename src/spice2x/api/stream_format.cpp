#include "stream_format.h"

#include <vector>

#include "h264_stream.h"
#include "hooks/graphics/jpeg_encoder.h"

namespace api {

    namespace {

#ifdef SPICE_JPEG
        constexpr const char *MJPEG_BOUNDARY = "spice2xframe";

        // multipart/x-mixed-replace: every frame is a standalone JPEG, no inter-frame state
        class MjpegWriter : public StreamWriter {
        public:

            explicit MjpegWriter(int quality) : quality(quality) {}

            std::string content_type() const override {
                return std::string("multipart/x-mixed-replace; boundary=") + MJPEG_BOUNDARY;
            }

            bool write(const StreamSend &send, const capture_pump::Frame &frame) override {
                this->jpeg.clear();
                if (!jpeg_encoder::encode(
                        this->jpeg, frame.pixels.get(),
                        frame.width, frame.height, this->quality)) {
                    // a frame the encoder rejects is not worth dropping the client over
                    return true;
                }

                const std::string part =
                        "--" + std::string(MJPEG_BOUNDARY) + "\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: " + std::to_string(this->jpeg.size()) + "\r\n"
                        "\r\n";

                return send(part.data(), part.size())
                        && send(this->jpeg.data(), this->jpeg.size())
                        && send("\r\n", 2);
            }

        private:
            int quality;
            std::vector<uint8_t> jpeg;
        };
#endif
    }

    // both parameters go unused on toolchains that compile in neither format
    std::unique_ptr<StreamWriter> make_stream_writer(
            const std::string &path, [[maybe_unused]] int quality, [[maybe_unused]] int fps) {

#ifdef SPICE_JPEG
        if (path == "/stream.mjpg") {
            return std::make_unique<MjpegWriter>(quality);
        }
#endif

#ifdef SPICE_H264
        if (path == "/stream.h264") {
            return make_h264_writer(quality, fps);
        }
#endif

        return nullptr;
    }

    std::vector<std::pair<std::string, std::string>> stream_formats() {
        std::vector<std::pair<std::string, std::string>> formats;

#ifdef SPICE_JPEG
        formats.emplace_back("mjpeg", "/stream.mjpg");
#endif

#ifdef SPICE_H264
        formats.emplace_back("h264", "/stream.h264");
#endif

        return formats;
    }
}
