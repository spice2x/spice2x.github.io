#include "downmix.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>

#include "util/logging.h"

namespace hooks::audio {

    // read one sample at `p` as a normalized float in [-1, 1]
    static inline float read_sample(const BYTE *p, int bytes, bool is_float) {
        if (is_float) {
            float v;
            memcpy(&v, p, sizeof(float));
            return v;
        }
        switch (bytes) {
            case 2: {
                int16_t v;
                memcpy(&v, p, sizeof(v));
                return v * (1.0f / 32768.0f);
            }
            case 3: {
                int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000) {
                    v |= ~0xFFFFFF; // sign extend
                }
                return v * (1.0f / 8388608.0f);
            }
            case 4: {
                int32_t v;
                memcpy(&v, p, sizeof(v));
                return (float) (v * (1.0 / 2147483648.0));
            }
            default:
                return 0.0f;
        }
    }

    // write the normalized float `value` to the sample at `p`, clamping to the format's range
    static inline void write_sample(BYTE *p, int bytes, bool is_float, float value) {
        if (is_float) {
            float v = std::clamp(value, -1.0f, 1.0f);
            memcpy(p, &v, sizeof(v));
            return;
        }
        switch (bytes) {
            case 2: {
                int16_t v = (int16_t) std::clamp(
                    (int) std::lround(value * 32768.0f), -32768, 32767);
                memcpy(p, &v, sizeof(v));
                break;
            }
            case 3: {
                int32_t v = (int32_t) std::clamp<int64_t>(
                    std::llround((double) value * 8388608.0), -8388608, 8388607);
                p[0] = v & 0xFF;
                p[1] = (v >> 8) & 0xFF;
                p[2] = (v >> 16) & 0xFF;
                break;
            }
            case 4: {
                int32_t v = (int32_t) std::clamp<int64_t>(
                    std::llround((double) value * 2147483648.0), INT32_MIN, INT32_MAX);
                memcpy(p, &v, sizeof(v));
                break;
            }
            default:
                break;
        }
    }

    void Downmix::setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *stereo_out,
            DownmixAlgorithm algorithm) {
        this->enabled = true;
        this->algorithm = algorithm;
        this->bytes_per_sample = game_format->wBitsPerSample / 8;
        this->game_frame_size = game_format->nChannels * this->bytes_per_sample;

        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT has Data1 == 3 (matches apply_gain detection)
        this->is_float = game_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
            || (game_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                && reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(game_format)
                    ->SubFormat.Data1 == 0x00000003);

        // supported: 16/24/32-bit integer PCM and 32-bit float; anything else mixes to silence
        const bool supported = this->is_float
            ? this->bytes_per_sample == 4
            : (this->bytes_per_sample >= 2 && this->bytes_per_sample <= 4);
        if (!supported) {
            log_fatal(
                "audio::downmix",
                "unsupported sample format ({}-bit {}), downmix will output silence",
                game_format->wBitsPerSample, this->is_float ? "float" : "int");
        }

        this->left_mix.clear();
        this->right_mix.clear();
        this->build_layout_mix(game_format);

        make_stereo_format(game_format, stereo_out);
    }

    void Downmix::make_stereo_format(const WAVEFORMATEX *game_format,
            WAVEFORMATEXTENSIBLE *stereo_out) {
        const int bytes_per_sample = game_format->wBitsPerSample / 8;

        memcpy(stereo_out, game_format, sizeof(WAVEFORMATEXTENSIBLE));
        stereo_out->Format.nChannels = 2;
        stereo_out->Format.nBlockAlign = 2 * bytes_per_sample;
        stereo_out->Format.nAvgBytesPerSec =
            game_format->nSamplesPerSec * stereo_out->Format.nBlockAlign;
        stereo_out->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    }

    HRESULT Downmix::initialize(IAudioClient *real, AUDCLNT_SHAREMODE share_mode, DWORD stream_flags,
            REFERENCE_TIME buffer_duration, REFERENCE_TIME periodicity,
            const WAVEFORMATEX *device_format, LPCGUID session_guid) {

        HRESULT ret = real->Initialize(share_mode, stream_flags, buffer_duration, periodicity,
                device_format, session_guid);

        // the smaller stereo buffer can end up unaligned for the device when the game sized the
        // duration for its larger multi-channel format. recover by asking the device for the next
        // aligned buffer size and re-initializing with a matching duration.
        if (ret == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 aligned_frames = 0;
            if (SUCCEEDED(real->GetBufferSize(&aligned_frames)) && aligned_frames > 0) {
                REFERENCE_TIME aligned_duration = (REFERENCE_TIME)
                        (10000.0 * 1000 / device_format->nSamplesPerSec * aligned_frames + 0.5);

                log_info("audio::downmix",
                        "buffer not aligned, retrying with {} frames ({} hns)",
                        aligned_frames, aligned_duration);

                ret = real->Initialize(share_mode, stream_flags, aligned_duration,
                        periodicity != 0 ? aligned_duration : 0, device_format, session_guid);
            }
        }

        return ret;
    }

    void Downmix::build_layout_mix(const WAVEFORMATEX *game_format) {

        const int channels = game_format->nChannels;

        constexpr float att = 0.70710678f; // -3 dB

        // speakers routed to the left/right output; anything else (center) feeds both sides
        constexpr DWORD left_speakers = SPEAKER_FRONT_LEFT | SPEAKER_BACK_LEFT | SPEAKER_SIDE_LEFT
            | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_TOP_FRONT_LEFT | SPEAKER_TOP_BACK_LEFT;
        constexpr DWORD right_speakers = SPEAKER_FRONT_RIGHT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_RIGHT
            | SPEAKER_FRONT_RIGHT_OF_CENTER | SPEAKER_TOP_FRONT_RIGHT | SPEAKER_TOP_BACK_RIGHT;

        // speakers kept by the extract-only modes; the other algorithms keep every channel.
        // "front" is the front left/right pair only, not the center speakers.
        DWORD keep = ~0u;
        switch (this->algorithm) {
            case DownmixAlgorithm::FrontOnly:
                keep = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                break;
            case DownmixAlgorithm::RearOnly:
                keep = SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
                break;
            case DownmixAlgorithm::SideOnly:
                keep = SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
                break;
            default:
                break;
        }

        // route one source channel into the output side(s) for its speaker at the given gain
        auto route = [&](int ch, DWORD speaker, float gain) {
            if (speaker & left_speakers) {
                this->left_mix.push_back({ ch, gain });
            } else if (speaker & right_speakers) {
                this->right_mix.push_back({ ch, gain });
            } else { // center: feed both sides
                this->left_mix.push_back({ ch, gain });
                this->right_mix.push_back({ ch, gain });
            }
        };

        // the speaker mask is only present on WAVE_FORMAT_EXTENSIBLE formats
        DWORD mask = 0;
        if (game_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                && game_format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(game_format)->dwChannelMask;
        }

        // without a mask the layout is unknown: fold interleaved L/R pairs. AC-4 attenuates by
        // -3 dB, the other algorithms keep unity gain.
        if (mask == 0) {
            const float gain = (this->algorithm == DownmixAlgorithm::AC4) ? att : 1.0f;
            for (int ch = 0; ch < channels; ch++) {
                (((ch & 1) == 0) ? this->left_mix : this->right_mix).push_back({ ch, gain });
            }
            return;
        }

        // each set speaker bit, in order, maps to the next source channel
        int channel = 0;
        for (int bit = 0; bit < 18 && channel < channels; bit++) {
            const DWORD speaker = 1u << bit;
            if ((mask & speaker) == 0) {
                continue;
            }

            const int ch = channel++;

            // LFE is always dropped; extract modes skip channels outside their group
            if (speaker == SPEAKER_LOW_FREQUENCY || (speaker & keep) == 0) {
                continue;
            }

            // AC-4 keeps the front pair at unity and attenuates everything else by -3 dB
            const bool front_pair = speaker & (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
            const float gain =
                (this->algorithm == DownmixAlgorithm::AC4 && !front_pair) ? att : 1.0f;

            route(ch, speaker, gain);
        }
    }

    void Downmix::process(BYTE *dst, const BYTE *src, UINT32 frames) const {
        const int bps = this->bytes_per_sample;
        const int src_stride = this->game_frame_size;
        const int dst_stride = 2 * bps;

        if (dst == nullptr || src == nullptr || bps <= 0) {
            return;
        }

        // sum each speaker's source channels into the matching stereo output
        for (UINT32 i = 0; i < frames; i++) {
            const BYTE *in = src + (size_t) i * src_stride;
            BYTE *out = dst + (size_t) i * dst_stride;

            float left = 0.0f;
            float right = 0.0f;
            for (const auto &c : this->left_mix) {
                left += read_sample(in + c.channel * bps, bps, this->is_float) * c.gain;
            }
            for (const auto &c : this->right_mix) {
                right += read_sample(in + c.channel * bps, bps, this->is_float) * c.gain;
            }
            write_sample(out, bps, this->is_float, left);
            write_sample(out + bps, bps, this->is_float, right);
        }
    }

    HRESULT Downmix::get_buffer(IAudioRenderClient *real, UINT32 frames, BYTE **ppData) {
        const size_t needed = (size_t) frames * this->game_frame_size;
        if (this->scratch.size() < needed) {
            this->scratch.resize(needed);
        }

        HRESULT ret = real->GetBuffer(frames, &this->device_buffer);
        if (FAILED(ret)) {
            this->device_buffer = nullptr;
            return ret;
        }

        *ppData = this->scratch.data();

        return S_OK;
    }

    void Downmix::write_device_buffer(UINT32 frames, DWORD flags) {
        const int bps = this->bytes_per_sample;
        const int dst_stride = 2 * bps;

        if (this->device_buffer == nullptr || frames == 0 || bps <= 0) {
            return;
        }

        // mute the first few buffers to avoid a pop on stream start
        if (this->buffers_to_mute > 0) {
            memset(this->device_buffer, 0, (size_t) frames * dst_stride);
            this->buffers_to_mute--;
        } else if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
            this->process(this->device_buffer, this->scratch.data(), frames);
        }
    }
}
