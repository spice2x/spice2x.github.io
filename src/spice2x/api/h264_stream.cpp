#include "h264_stream.h"

#ifdef SPICE_H264

#include <vector>

#include <x264.h>

#include "util/logging.h"

namespace api {

    namespace {

        // BT.601 limited range, the range every decoder assumes for H.264 without
        // explicit colour metadata
        inline uint8_t rgb_to_y(int r, int g, int b) {
            return static_cast<uint8_t>(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }

        inline uint8_t rgb_to_u(int r, int g, int b) {
            return static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
        }

        inline uint8_t rgb_to_v(int r, int g, int b) {
            return static_cast<uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }

        // a bare annex-b elementary stream, one encoder per connection so every client
        // starts on its own keyframe. no container, so nothing here keeps a media clock
        class H264Writer : public StreamWriter {
        public:

            H264Writer(int quality, int fps) : quality(quality), fps(fps) {}

            ~H264Writer() override {
                this->close();
            }

            std::string content_type() const override {
                return "video/h264";
            }

            bool write(const StreamSend &send, const capture_pump::Frame &frame) override {

                // I420 needs even dimensions
                const int width = frame.width & ~1;
                const int height = frame.height & ~1;
                if (width <= 0 || height <= 0) {
                    return true;
                }

                if (this->encoder == nullptr) {
                    if (!this->open(width, height)) {
                        return false;
                    }
                } else if (width != this->width || height != this->height) {
                    // the encoder is fixed at the size it opened with; let the client reconnect
                    log_info("api::stream", "capture size changed, ending H.264 client");
                    return false;
                }

                this->convert(frame.pixels.get(), frame.width);

                this->picture.i_pts = this->frame_index;

                x264_nal_t *nals = nullptr;
                int nal_count = 0;
                x264_picture_t picture_out;
                const int size = x264_encoder_encode(
                        this->encoder, &nals, &nal_count, &this->picture, &picture_out);

                if (size < 0) {
                    log_warning("api::stream", "H.264 encode failed");
                    return false;
                }

                this->frame_index++;

                if (size == 0) {
                    return true;
                }

                // x264 lays every NAL of the frame out back to back. an SEI or delimiter
                // carries no picture, so only the parameter sets and the slice go through
                this->annexb.clear();
                for (int i = 0; i < nal_count; i++) {
                    switch (nals[i].i_type) {
                        case NAL_SEI:
                        case NAL_AUD:
                        case NAL_FILLER:
                            continue;
                        default:
                            break;
                    }
                    this->annexb.insert(this->annexb.end(),
                            nals[i].p_payload, nals[i].p_payload + nals[i].i_payload);
                }

                if (this->annexb.empty()) {
                    return true;
                }

                return send(this->annexb.data(), this->annexb.size());
            }

        private:

            bool open(int width, int height) {

                x264_param_t param;
                if (x264_param_default_preset(&param, "ultrafast", "zerolatency") < 0) {
                    return false;
                }

                param.i_csp = X264_CSP_I420;
                param.i_width = width;
                param.i_height = height;
                param.i_fps_num = this->fps;
                param.i_fps_den = 1;

                // sliced threading, which zerolatency already selected, so a frame is split
                // across workers rather than held back to be reordered. deliberately not the
                // automatic count: this shares a machine with the game it is capturing, and
                // taking every core to encode would win back frames at the game's expense
                param.i_threads = 4;

                param.b_annexb = 1;
                // SPS/PPS ahead of every IDR, so a client can start decoding cold
                param.b_repeat_headers = 1;
                // a keyframe every two seconds bounds how long a new client waits
                param.i_keyint_max = this->fps * 2;
                param.i_log_level = X264_LOG_NONE;
                param.rc.i_rc_method = X264_RC_CRF;
                param.rc.f_rf_constant = 40.0f - (this->quality * 0.25f);

                // baseline keeps hardware decode available on the widest range of phones
                if (x264_param_apply_profile(&param, "baseline") < 0) {
                    return false;
                }

                this->encoder = x264_encoder_open(&param);
                if (this->encoder == nullptr) {
                    log_warning("api::stream", "could not open the H.264 encoder");
                    return false;
                }

                if (x264_picture_alloc(&this->picture, X264_CSP_I420, width, height) < 0) {
                    this->close();
                    return false;
                }
                this->picture_ready = true;

                this->width = width;
                this->height = height;
                return true;
            }

            void close() {
                if (this->picture_ready) {
                    x264_picture_clean(&this->picture);
                    this->picture_ready = false;
                }
                if (this->encoder != nullptr) {
                    x264_encoder_close(this->encoder);
                    this->encoder = nullptr;
                }
            }

            // packed 24bpp RGB to I420, averaging each 2x2 block for the chroma planes
            void convert(const uint8_t *rgb, int source_width) {

                uint8_t *plane_y = this->picture.img.plane[0];
                uint8_t *plane_u = this->picture.img.plane[1];
                uint8_t *plane_v = this->picture.img.plane[2];
                const int stride_y = this->picture.img.i_stride[0];
                const int stride_u = this->picture.img.i_stride[1];
                const int stride_v = this->picture.img.i_stride[2];

                for (int y = 0; y < this->height; y++) {
                    const uint8_t *row = rgb + static_cast<size_t>(y) * source_width * 3;
                    uint8_t *out_y = plane_y + static_cast<size_t>(y) * stride_y;

                    for (int x = 0; x < this->width; x++) {
                        const uint8_t *pixel = row + x * 3;
                        out_y[x] = rgb_to_y(pixel[0], pixel[1], pixel[2]);
                    }
                }

                for (int y = 0; y < this->height / 2; y++) {
                    const uint8_t *row0 = rgb + static_cast<size_t>(y * 2) * source_width * 3;
                    const uint8_t *row1 = row0 + static_cast<size_t>(source_width) * 3;
                    uint8_t *out_u = plane_u + static_cast<size_t>(y) * stride_u;
                    uint8_t *out_v = plane_v + static_cast<size_t>(y) * stride_v;

                    for (int x = 0; x < this->width / 2; x++) {
                        const uint8_t *p00 = row0 + (x * 2) * 3;
                        const uint8_t *p01 = p00 + 3;
                        const uint8_t *p10 = row1 + (x * 2) * 3;
                        const uint8_t *p11 = p10 + 3;

                        const int r = (p00[0] + p01[0] + p10[0] + p11[0] + 2) / 4;
                        const int g = (p00[1] + p01[1] + p10[1] + p11[1] + 2) / 4;
                        const int b = (p00[2] + p01[2] + p10[2] + p11[2] + 2) / 4;

                        out_u[x] = rgb_to_u(r, g, b);
                        out_v[x] = rgb_to_v(r, g, b);
                    }
                }
            }

            int quality;
            int fps;
            int width = 0;
            int height = 0;
            int64_t frame_index = 0;
            std::vector<uint8_t> annexb;

            x264_t *encoder = nullptr;
            x264_picture_t picture {};
            bool picture_ready = false;
        };
    }

    std::unique_ptr<StreamWriter> make_h264_writer(int quality, int fps) {
        return std::make_unique<H264Writer>(quality, fps);
    }
}

#endif // SPICE_H264
