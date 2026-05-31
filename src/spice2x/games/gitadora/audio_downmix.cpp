#include "audio_downmix.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>

#include "games/gitadora/gitadora.h"

namespace games::gitadora {

    // the game always outputs a 7.1 (8-channel) surround frame:
    // FL(0), FR(1), FC, LFE, BL(4), BR(5), SL, SR. the front and rear channels are summed
    // together into the stereo front speakers.
    static constexpr int SURROUND_CHANNELS = 8;
    static constexpr int FRONT_LEFT_INDEX = 0;
    static constexpr int FRONT_RIGHT_INDEX = 1;
    static constexpr int REAR_LEFT_INDEX = 4;
    static constexpr int REAR_RIGHT_INDEX = 5;

    bool ArenaDownmix::is_downmix_required(const WAVEFORMATEX *format) {
        return format
            && games::gitadora::TWOCHANNEL
            && format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
            && format->nChannels == SURROUND_CHANNELS;
    }

    void ArenaDownmix::fix_legacy_mask(WAVEFORMATEX *format) {
        if (!format) {
            return;
        }
        if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
            return;
        }

        auto ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);
        if (ext->dwChannelMask == KSAUDIO_SPEAKER_7POINT1) {
            ext->dwChannelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
        }
    }

    void ArenaDownmix::setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *stereo_out) {
        this->enabled = true;
        this->bytes_per_sample = game_format->wBitsPerSample / 8;
        this->game_frame_size = game_format->nChannels * this->bytes_per_sample;

        memcpy(stereo_out, game_format, sizeof(WAVEFORMATEXTENSIBLE));
        stereo_out->Format.nChannels = 2;
        stereo_out->Format.nBlockAlign = 2 * this->bytes_per_sample;
        stereo_out->Format.nAvgBytesPerSec =
            game_format->nSamplesPerSec * stereo_out->Format.nBlockAlign;
        stereo_out->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    }

    void ArenaDownmix::process(BYTE *dst, const BYTE *src, UINT32 frames) const {
        const int src_stride = this->game_frame_size;

        // mixing has to interpret the samples; we only support the game's 16-bit PCM format
        if (dst == nullptr || src == nullptr || this->bytes_per_sample != 2) {
            return;
        }

        auto in = reinterpret_cast<const int16_t *>(src);
        auto out = reinterpret_cast<int16_t *>(dst);
        const int step = src_stride / 2;

        // sum the 7.1 front and rear channels into the stereo front speakers
        for (UINT32 i = 0; i < frames; i++, in += step, out += 2) {
            out[0] = (int16_t) std::clamp(in[FRONT_LEFT_INDEX] + in[REAR_LEFT_INDEX], -32768, 32767);
            out[1] = (int16_t) std::clamp(in[FRONT_RIGHT_INDEX] + in[REAR_RIGHT_INDEX], -32768, 32767);
        }
    }

    HRESULT ArenaDownmix::get_buffer(IAudioRenderClient *real, UINT32 frames, BYTE **ppData) {
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

    HRESULT ArenaDownmix::release_buffer(IAudioRenderClient *real, UINT32 frames, DWORD flags) {
        const int bps = this->bytes_per_sample;
        const int dst_stride = 2 * bps;

        if (this->device_buffer != nullptr && frames > 0 && bps > 0) {

            // fix for audio pop effect: mute the first few buffers
            if (this->buffers_to_mute > 0) {
                memset(this->device_buffer, 0, (size_t) frames * dst_stride);
                this->buffers_to_mute--;
            } else if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
                this->process(this->device_buffer, this->scratch.data(), frames);
            }
        }

        HRESULT ret = real->ReleaseBuffer(frames, flags);
        this->device_buffer = nullptr;

        return ret;
    }
}
