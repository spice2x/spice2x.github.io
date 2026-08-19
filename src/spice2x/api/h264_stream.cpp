#include "h264_stream.h"

#ifdef SPICE_H264

#include <algorithm>
#include <vector>

#include <x264.h>

#define MINIMP4_IMPLEMENTATION
#include "external/minimp4/minimp4.h"

#include "util/logging.h"

namespace api {

    namespace {

        constexpr int TIMESCALE_90KHZ = 90000;

        // BT.601 limited range, the range every browser assumes for H.264 without
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

        // one encoder and muxer per connection, so every client gets its own init segment and
        // starts on a keyframe. with `container` off the output is bare annex-b instead
        class H264Writer : public StreamWriter {
        public:

            H264Writer(int quality, int fps, bool container)
                : quality(quality), fps(fps), container(container) {}

            ~H264Writer() override {
                this->close();
            }

            std::string content_type() const override {
                return this->container ? "video/mp4" : "video/h264";
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

                // annex-b: x264 lays every NAL of the frame out back to back.
                // in fragmentation mode minimp4 makes a separate sample out of every NAL it is
                // given, so an SEI or delimiter becomes a fragment holding no picture and
                // decoders reject it. only the parameter sets and the slice itself go through,
                // which a raw decoder is equally happy with
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

                // no container, so no media clock to keep: the client shows each frame on decode
                if (!this->container) {
                    return send(this->annexb.data(), this->annexb.size());
                }

                // minimp4 names this argument a timestamp but uses it as the sample duration.
                // capture never hits the requested fps exactly, so timing each frame from its
                // own capture time keeps the media clock on wall clock instead of drifting
                const uint64_t nominal = TIMESCALE_90KHZ / this->fps;
                unsigned frame_duration = static_cast<unsigned>(nominal);
                if (this->last_timestamp != 0 && frame.timestamp > this->last_timestamp) {
                    // the gap before a frame really belongs to the one before it, so a capture
                    // stall would otherwise be charged to the frame that ends it
                    const uint64_t delta = (frame.timestamp - this->last_timestamp) * 90;
                    frame_duration = static_cast<unsigned>(
                            std::clamp<uint64_t>(delta, 900, nominal * 4));
                }
                this->last_timestamp = frame.timestamp;

                if (mp4_h26x_write_nal(
                        &this->muxer, this->annexb.data(),
                        static_cast<int>(this->annexb.size()), frame_duration)
                        != MP4E_STATUS_OK) {
                    log_warning("api::stream", "H.264 muxing failed");
                    return false;
                }

                return this->flush(send);
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
                param.i_threads = 1;
                param.b_annexb = 1;
                // SPS/PPS ahead of every IDR, which is where minimp4 picks them up
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

                if (this->container) {
                    this->mux = MP4E_open(1, 1, this, &H264Writer::write_callback);
                    if (this->mux == nullptr) {
                        this->close();
                        return false;
                    }

                    if (mp4_h26x_write_init(&this->muxer, this->mux, width, height, 0)
                            != MP4E_STATUS_OK) {
                        this->close();
                        return false;
                    }
                    this->muxer_ready = true;
                }

                this->width = width;
                this->height = height;
                return true;
            }

            void close() {
                if (this->muxer_ready) {
                    mp4_h26x_write_close(&this->muxer);
                    this->muxer_ready = false;
                }
                if (this->mux != nullptr) {
                    MP4E_close(this->mux);
                    this->mux = nullptr;
                }
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

            bool flush(const StreamSend &send) {
                if (this->pending.empty()) {
                    return true;
                }

                const bool ok = send(this->pending.data(), this->pending.size());
                this->pending.clear();
                return ok;
            }

            static int write_callback(
                    int64_t offset, const void *buffer, size_t size, void *token) {

                auto self = static_cast<H264Writer *>(token);

                // a socket cannot seek; fragmented output should only ever append
                if (offset != self->written) {
                    log_warning("api::stream",
                            "H.264 muxer seeked to {} instead of {}, ending client",
                            offset, self->written);
                    return 1;
                }

                auto bytes = static_cast<const uint8_t *>(buffer);
                self->pending.insert(self->pending.end(), bytes, bytes + size);
                self->written += static_cast<int64_t>(size);
                return 0;
            }

            int quality;
            int fps;
            bool container;
            int width = 0;
            int height = 0;
            int64_t frame_index = 0;
            uint64_t last_timestamp = 0;
            int64_t written = 0;
            std::vector<uint8_t> annexb;

            x264_t *encoder = nullptr;
            x264_picture_t picture {};
            bool picture_ready = false;

            MP4E_mux_t *mux = nullptr;
            mp4_h26x_writer_t muxer {};
            bool muxer_ready = false;

            std::vector<uint8_t> pending;
        };
    }

    std::unique_ptr<StreamWriter> make_h264_writer(int quality, int fps) {
        return std::make_unique<H264Writer>(quality, fps, true);
    }

    std::unique_ptr<StreamWriter> make_annexb_writer(int quality, int fps) {
        return std::make_unique<H264Writer>(quality, fps, false);
    }
}

#endif // SPICE_H264
