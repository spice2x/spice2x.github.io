#include "resample.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <audioclient.h>

#include "util/logging.h"

#include "util.h"

namespace hooks::audio {

    namespace {

        constexpr double PI = 3.14159265358979323846;

        // normalized sinc: sin(pi*x) / (pi*x), with the removable singularity at 0 filled in
        inline double sinc(double x) {
            if (x == 0.0) {
                return 1.0;
            }
            const double px = PI * x;
            return std::sin(px) / px;
        }

        // Blackman window across the kernel radius; zero at +/- radius
        inline double blackman(double x, double radius) {
            const double n = (x + radius) / (2.0 * radius);
            if (n <= 0.0 || n >= 1.0) {
                return 0.0;
            }
            return 0.42 - 0.5 * std::cos(2.0 * PI * n) + 0.08 * std::cos(4.0 * PI * n);
        }
    }

    std::optional<uint32_t> Resampler::resolve(const WAVEFORMATEX *game_format) {
        if (game_format == nullptr || !RESAMPLE_RATE.has_value()) {
            return std::nullopt;
        }
        if (game_format->nSamplesPerSec == 0
                || game_format->nSamplesPerSec == RESAMPLE_RATE.value()) {
            return std::nullopt;
        }
        return RESAMPLE_RATE;
    }

    void Resampler::setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *device_out,
            uint32_t target_rate) {
        this->enabled = true;
        this->channels = game_format->nChannels;
        this->bytes_per_sample = game_format->wBitsPerSample / 8;
        this->game_frame_size = this->channels * this->bytes_per_sample;

        this->is_float = is_ieee_float(game_format);

        const bool supported = this->is_float
            ? this->bytes_per_sample == 4
            : (this->bytes_per_sample >= 2 && this->bytes_per_sample <= 4);
        if (!supported) {
            log_fatal(
                "audio::resample",
                "unsupported sample format ({}-bit {}) for -resample",
                game_format->wBitsPerSample, this->is_float ? "float" : "int");
        }

        this->src_rate = game_format->nSamplesPerSec;
        this->dst_rate = target_rate;

        // anti-alias cutoff: full bandwidth when upsampling, scaled down when decimating
        this->cutoff = std::min(1.0, (double) this->dst_rate / (double) this->src_rate);
        this->half_taps = 16;

        // precompute the windowed-sinc kernel now that cutoff is known
        this->build_kernel();

        // prime the queue with half a window of silence so the first outputs have left history
        this->in_queue.assign((size_t) this->half_taps * this->channels, 0.0f);
        this->in_pos = this->half_taps;

        this->make_device_format(game_format, device_out, target_rate);
    }

    void Resampler::make_device_format(const WAVEFORMATEX *game_format,
            WAVEFORMATEXTENSIBLE *device_out, uint32_t target_rate) {
        const size_t src_size = sizeof(WAVEFORMATEX) + game_format->cbSize;

        memset(device_out, 0, sizeof(WAVEFORMATEXTENSIBLE));
        memcpy(device_out, game_format, std::min(src_size, sizeof(WAVEFORMATEXTENSIBLE)));

        device_out->Format.nSamplesPerSec = target_rate;
        device_out->Format.nAvgBytesPerSec = target_rate * device_out->Format.nBlockAlign;
    }

    HRESULT Resampler::initialize(IAudioClient *real, AUDCLNT_SHAREMODE share_mode,
            DWORD stream_flags, REFERENCE_TIME buffer_duration, REFERENCE_TIME periodicity,
            const WAVEFORMATEX *device_format, LPCGUID session_guid) {

        // the resampler bypasses the OS mixer and talks to the device directly, so it only makes
        // sense (and only works) for exclusive streams. shared streams are already resampled by
        // the Windows audio engine, so refuse loudly rather than silently doing nothing.
        if (share_mode != AUDCLNT_SHAREMODE_EXCLUSIVE) {
            log_fatal("audio::resample",
                    "-resample requires WASAPI exclusive mode, but this stream is shared "
                    "(Windows already resamples shared streams)");
        }

        // record the pacing model. event-driven streams fill the whole device buffer each period
        // (produce_exact); timer-driven streams poll padding and write variable partial chunks, so
        // they drain the pending output to the device's free space each call (flush_timer).
        this->event_driven = (stream_flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) != 0;

        return initialize_with_alignment_retry(real, "audio::resample", share_mode, stream_flags,
                buffer_duration, periodicity, device_format, session_guid);
    }

    UINT32 Resampler::frames_device_to_game(UINT32 device_frames) const {
        if (this->dst_rate == 0) {
            return device_frames;
        }
        // round down so the game never believes it has more room than the device can hold
        return (UINT32) (((double) device_frames * this->src_rate) / this->dst_rate);
    }

    UINT32 Resampler::padding_device_to_game(UINT32 device_padding) const {
        if (this->dst_rate == 0) {
            return device_padding;
        }
        // round up so the reported free space stays conservative
        return (UINT32) std::ceil(((double) device_padding * this->src_rate) / this->dst_rate);
    }

    HRESULT Resampler::get_buffer(UINT32 frames, BYTE **ppData) {
        const size_t needed = (size_t) frames * this->game_frame_size;
        if (this->scratch.size() < needed) {
            this->scratch.resize(needed);
        }

        *ppData = this->scratch.data();

        return S_OK;
    }

    void Resampler::enqueue_input(UINT32 frames, bool silent) {
        const int bps = this->bytes_per_sample;
        const int ch = this->channels;
        const size_t base = this->in_queue.size();

        this->in_queue.resize(base + (size_t) frames * ch);

        if (silent || bps <= 0 || ch <= 0) {
            std::fill(this->in_queue.begin() + base, this->in_queue.end(), 0.0f);
            return;
        }

        const BYTE *src = this->scratch.data();
        for (UINT32 f = 0; f < frames; f++) {
            for (int c = 0; c < ch; c++) {
                const size_t s = (size_t) f * ch + c;
                this->in_queue[base + s] = read_sample(src + s * bps, bps, this->is_float);
            }
        }
    }

    void Resampler::build_kernel() {
        const int taps = 2 * this->half_taps;
        const int phases = this->kernel_phases;
        const double cut = this->cutoff;
        const double radius = (double) this->half_taps;

        // one extra row at frac == 1.0 so emit_frame can interpolate against row p + 1 safely
        this->kernel_table.resize((size_t) (phases + 1) * taps);

        for (int p = 0; p <= phases; p++) {
            const double frac = (double) p / (double) phases;
            for (int k = 0; k < taps; k++) {
                // tap k maps to input offset t = k - (half_taps - 1), matching emit_frame
                const double x = frac - (double) (k - (this->half_taps - 1));
                this->kernel_table[(size_t) p * taps + k] =
                        (float) (cut * sinc(cut * x) * blackman(x, radius));
            }
        }
    }

    void Resampler::emit_frame() {
        const int ch = this->channels;
        const int radius = this->half_taps;
        const int taps = 2 * radius;
        const long avail = (long) (this->in_queue.size() / ch);
        const long center = (long) std::floor(this->in_pos);

        // pick the two kernel rows bracketing this fractional position and the blend between them
        const double frac = this->in_pos - (double) center;
        const double fp = frac * (double) this->kernel_phases;
        const int p0 = (int) fp;
        const float blend = (float) (fp - (double) p0);
        const float *row0 = &this->kernel_table[(size_t) p0 * taps];
        const float *row1 = &this->kernel_table[(size_t) (p0 + 1) * taps];

        // base input index for tap 0 (t = -(radius - 1))
        const long base = center - (radius - 1);

        for (int c = 0; c < ch; c++) {
            double acc = 0.0;
            for (int k = 0; k < taps; k++) {
                const long idx = base + k;
                if (idx < 0 || idx >= avail) {
                    continue;
                }
                const float w = row0[k] + blend * (row1[k] - row0[k]);
                acc += (double) this->in_queue[(size_t) idx * ch + c] * w;
            }
            this->out_float.push_back((float) acc);
        }
    }

    void Resampler::drop_consumed() {
        const int ch = this->channels;
        const long drop = (long) std::floor(this->in_pos) - this->half_taps;
        if (drop > 0) {
            const size_t drop_samples = (size_t) drop * ch;
            if (drop_samples <= this->in_queue.size()) {
                this->in_queue.erase(this->in_queue.begin(),
                        this->in_queue.begin() + drop_samples);
                this->in_pos -= drop;
            }
        }
    }

    UINT32 Resampler::produce_exact(UINT32 out_frames) {
        const int ch = this->channels;
        this->out_float.clear();
        if (ch <= 0 || out_frames == 0) {
            return 0;
        }
        this->out_float.reserve((size_t) out_frames * ch);

        // resample ratio. drive it from the buffer size actually advertised to the game rather
        // than the nominal src/dst ratio: GetBufferSize reports floor(dev_buf * src/dst) game
        // frames, so the game only ever delivers that many input frames per device period.
        // consuming at the nominal ratio would eat slightly more input than arrives on any device
        // where dev_buf * src/dst is non-integer (e.g. 144 -> 132.3, floored to 132), slowly
        // draining the queue until it underruns to permanent silence. using the advertised integer
        // ratio keeps input and output exactly balanced; the resulting pitch error is below 0.3%
        // and inaudible, and it collapses to the exact ratio when the division is integer (160 ->
        // 147 stays 147/160 = 44100/48000).
        const double step = (double) this->frames_device_to_game(this->device_buffer_frames)
                / (double) this->device_buffer_frames;

        // input frames the block will touch: from in_pos through the right edge of the sinc kernel
        // at the final output sample. if the queue is short of this, the kernel tail reads past the
        // end and distorts every buffer, so buffer one extra block of input before the first output
        // (emitting silence without consuming) to build a cushion the kernel can always reach into.
        const long avail = (long) (this->in_queue.size() / ch);
        const long need = (long) std::ceil(this->in_pos + step * (double) out_frames)
                + this->half_taps;

        if (this->priming) {
            if (avail < need + (long) out_frames) {
                this->out_float.assign((size_t) out_frames * ch, 0.0f);
                return out_frames;
            }
            this->priming = false;
        }

        for (UINT32 o = 0; o < out_frames; o++) {
            this->emit_frame();
            this->in_pos += step;
        }

        this->drop_consumed();
        return out_frames;
    }

    UINT32 Resampler::produce_variable() {
        const int ch = this->channels;
        if (ch <= 0) {
            return 0;
        }

        // input frames consumed per output frame. timer-driven streams write variable partial
        // chunks, so produce however many output frames the currently queued input can fully
        // support and leave the rest for the next call; this keeps input and output balanced at
        // the exact src/dst ratio over time without depending on the device buffer size.
        const double step = (double) this->src_rate / (double) this->dst_rate;
        const long avail = (long) (this->in_queue.size() / ch);

        // emit only while the sinc kernel's right edge stays within the queued input. the kernel
        // reaches from in_pos out to half_taps frames ahead, so stop once that would read past the
        // end; the remaining input becomes the next block's lookahead.
        UINT32 produced = 0;
        while ((long) std::ceil(this->in_pos) + this->half_taps < avail) {
            this->emit_frame();
            this->in_pos += step;
            produced++;
        }

        this->drop_consumed();
        return produced;
    }

    void Resampler::write_output(BYTE *dst, UINT32 frames, float gain) const {
        const int bps = this->bytes_per_sample;
        const int ch = this->channels;
        const size_t count = (size_t) frames * ch;

        for (size_t i = 0; i < count; i++) {
            write_sample(dst + i * bps, bps, this->is_float, this->out_float[i] * gain);
        }
    }

    HRESULT Resampler::flush(IAudioRenderClient *real, IAudioClient *client, UINT32 frames,
            DWORD flags, float boost) {
        if (!this->enabled) {
            return S_OK;
        }

        // cache the device buffer size once
        if (this->device_buffer_frames == 0) {
            client->GetBufferSize(&this->device_buffer_frames);
        }
        if (this->device_buffer_frames == 0) {
            return S_OK;
        }

        const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        this->enqueue_input(frames, silent);

        // confirm once that conversion actually started producing output
        static std::once_flag active_printed;
        std::call_once(active_printed, [this]() {
            log_info("audio::resample", "resample active: {} Hz -> {} Hz ({} ch, {})",
                    this->src_rate, this->dst_rate, this->channels,
                    this->event_driven ? "event-driven" : "timer-driven");
        });

        // the boost is applied here (inside write_output) rather than in the standard ReleaseBuffer
        // path, so log it once for parity with that path's "volume boost active" line.
        if (boost != 1.0f) {
            static std::once_flag boost_printed;
            std::call_once(boost_printed, [boost]() {
                log_info("audio::resample", "volume boost active (resample): gain={}", boost);
            });
        }

        return this->event_driven
                ? this->flush_event(real, boost)
                : this->flush_timer(real, client, boost);
    }

    HRESULT Resampler::flush_event(IAudioRenderClient *real, float boost) {

        // event-driven exclusive streams must hand the device a full buffer every period and may
        // not push partial counts. resample the whole input block into exactly the device buffer
        // size.
        const UINT32 produced = this->produce_exact(this->device_buffer_frames);
        if (produced == 0) {
            return S_OK;
        }

        BYTE *dev = nullptr;
        HRESULT ret = real->GetBuffer(produced, &dev);
        if (FAILED(ret) || dev == nullptr) {
            return ret;
        }

        // mute the first few buffers to avoid a pop on stream start
        float gain = boost;
        if (this->buffers_to_mute > 0) {
            gain = 0.0f;
            this->buffers_to_mute--;
        }

        this->write_output(dev, produced, gain);

        return real->ReleaseBuffer(produced, 0);
    }

    HRESULT Resampler::flush_timer(IAudioRenderClient *real, IAudioClient *client, float boost) {

        // convert everything currently queued into the pending output FIFO (out_float). timer-
        // driven games write variable partial chunks, so produce only what the queued input can
        // fully support and keep the remainder for the next call.
        this->produce_variable();

        const int ch = this->channels;
        if (ch <= 0) {
            return S_OK;
        }

        const UINT32 pending = (UINT32) (this->out_float.size() / ch);
        if (pending == 0) {
            return S_OK;
        }

        // push as many frames as the device currently has free, keeping the rest queued for the
        // next call. timer-driven games poll padding and write whenever there is room, so matching
        // the device's free space here avoids overflowing the ring while staying device-paced.
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) {
            return S_OK;
        }
        const UINT32 device_free = this->device_buffer_frames > padding
                ? this->device_buffer_frames - padding
                : 0;
        if (device_free == 0) {
            return S_OK;
        }

        const UINT32 to_write = std::min(pending, device_free);

        BYTE *dev = nullptr;
        HRESULT ret = real->GetBuffer(to_write, &dev);
        if (FAILED(ret) || dev == nullptr) {
            return ret;
        }

        // mute the first few buffers to avoid a pop on stream start
        float gain = boost;
        if (this->buffers_to_mute > 0) {
            gain = 0.0f;
            this->buffers_to_mute--;
        }

        this->write_output(dev, to_write, gain);
        ret = real->ReleaseBuffer(to_write, 0);

        // drop the frames just written from the front of the pending FIFO
        this->out_float.erase(this->out_float.begin(),
                this->out_float.begin() + (size_t) to_write * ch);

        return ret;
    }
}
