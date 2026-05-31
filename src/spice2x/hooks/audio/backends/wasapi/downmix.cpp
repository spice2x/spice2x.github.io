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

    void Downmix::setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *stereo_out) {
        this->enabled = true;
        this->bytes_per_sample = game_format->wBitsPerSample / 8;
        this->game_frame_size = game_format->nChannels * this->bytes_per_sample;

        if (this->bytes_per_sample != 2) {
            log_fatal(
                "audio::downmix",
                "unsupported sample format ({}-bit), downmix will output silence",
                game_format->wBitsPerSample);
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

        // AC-4 stereo downmix coefficients (ETSI TS 103 190-1 §6.2.17, spec default values):
        //   front left/right -> same side at  0 dB
        //   center           -> both sides at -3 dB
        //   surrounds        -> same side at  -3 dB
        //   LFE              -> dropped (-inf dB)
        constexpr float att = 0.70710678f; // -3 dB

        // speaker positions that fold into one side at -3 dB
        constexpr DWORD surround_left = SPEAKER_BACK_LEFT | SPEAKER_SIDE_LEFT
            | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_TOP_FRONT_LEFT | SPEAKER_TOP_BACK_LEFT;
        constexpr DWORD surround_right = SPEAKER_BACK_RIGHT | SPEAKER_SIDE_RIGHT
            | SPEAKER_FRONT_RIGHT_OF_CENTER | SPEAKER_TOP_FRONT_RIGHT | SPEAKER_TOP_BACK_RIGHT;

        // the speaker mask is only present on WAVE_FORMAT_EXTENSIBLE formats
        DWORD mask = 0;
        if (game_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                && game_format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(game_format)->dwChannelMask;
        }

        // without a mask, assume interleaved L/R pairs and fold each pair down at -3 dB
        if (mask == 0) {
            for (int ch = 0; ch < channels; ch++) {
                (((ch & 1) == 0) ? this->left_mix : this->right_mix).push_back({ ch, att });
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

            if (speaker == SPEAKER_FRONT_LEFT) {
                this->left_mix.push_back({ channel, 1.0f });
            } else if (speaker == SPEAKER_FRONT_RIGHT) {
                this->right_mix.push_back({ channel, 1.0f });
            } else if (speaker & surround_left) {
                this->left_mix.push_back({ channel, att });
            } else if (speaker & surround_right) {
                this->right_mix.push_back({ channel, att });
            } else if (speaker != SPEAKER_LOW_FREQUENCY) {
                // center (or unknown) goes to both speakers; LFE is dropped
                this->left_mix.push_back({ channel, att });
                this->right_mix.push_back({ channel, att });
            }

            channel++;
        }
    }

    void Downmix::process(BYTE *dst, const BYTE *src, UINT32 frames) const {
        const int src_stride = this->game_frame_size;

        // we only handle the game's 16-bit PCM samples
        if (dst == nullptr || src == nullptr || this->bytes_per_sample != 2) {
            return;
        }

        auto in = reinterpret_cast<const int16_t *>(src);
        auto out = reinterpret_cast<int16_t *>(dst);
        const int step = src_stride / 2;

        // sum each speaker's source channels into the matching stereo output
        for (UINT32 i = 0; i < frames; i++, in += step, out += 2) {
            float left = 0.0f;
            float right = 0.0f;
            for (const auto &c : this->left_mix) {
                left += in[c.channel] * c.gain;
            }
            for (const auto &c : this->right_mix) {
                right += in[c.channel] * c.gain;
            }
            out[0] = (int16_t) std::clamp((int) std::lround(left), -32768, 32767);
            out[1] = (int16_t) std::clamp((int) std::lround(right), -32768, 32767);
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
