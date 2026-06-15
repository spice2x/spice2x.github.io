#include "shared.h"

#include <algorithm>

#include <audioclient.h>

#include "hooks/audio/audio.h"
#include "util/logging.h"

#include "util.h"
#include "defs.h"

namespace hooks::audio {

    // whether the engine's PCM converter can handle this format. PCM / float only; non-PCM
    // bitstream (AC-3 / DTS passthrough) must be left alone.
    static bool is_pcm_or_float(const WAVEFORMATEX *format) {
        if (format == nullptr) {
            return false;
        }

        switch (format->wFormatTag) {
            case WAVE_FORMAT_PCM:
            case WAVE_FORMAT_IEEE_FLOAT:
                return true;
            case WAVE_FORMAT_EXTENSIBLE: {

                // SubFormat is only valid when the extra-bytes block is large enough
                if (format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
                    return false;
                }
                const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
                return ext->SubFormat == GUID_KSDATAFORMAT_SUBTYPE_PCM
                        || ext->SubFormat == GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            }
            default:
                return false;
        }
    }

    bool SharedRedirect::wants(AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX *format) {

        // only redirect PCM / float exclusive streams: the engine converter (AUTOCONVERTPCM) can
        // handle those, but non-PCM bitstream (AC-3 / DTS passthrough) would fail in shared mode,
        // so leave it in exclusive untouched.
        return hooks::audio::WASAPI_COMPATIBILITY_MODE
                && share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE
                && is_pcm_or_float(format);
    }

    void SharedRedirect::apply(AUDCLNT_SHAREMODE *share_mode, DWORD *stream_flags,
            REFERENCE_TIME *periodicity) {

        // shared mode requires periodicity == 0; AUTOCONVERTPCM lets the engine accept the game's
        // native format (else shared Initialize returns AUDCLNT_E_UNSUPPORTED_FORMAT).
        log_info("audio::wasapi", "redirecting exclusive WASAPI to shared mode");
        *share_mode = AUDCLNT_SHAREMODE_SHARED;
        *periodicity = 0;
        *stream_flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        this->redirected_from_exclusive = true;
    }

    UINT32 SharedRedirect::clamp_buffer_size(IAudioClient *real, uint32_t sample_rate,
            UINT32 device_frames) {
        if (!this->redirected_from_exclusive || real == nullptr || sample_rate == 0 || device_frames == 0) {
            this->reported_frames = device_frames;
            return device_frames;
        }

        // GetDevicePeriod returns REFERENCE_TIME units (100 ns), 10^7 per second, so
        // period_frames = period * sample_rate / 10^7.
        REFERENCE_TIME period = 0;
        if (SUCCEEDED(real->GetDevicePeriod(&period, nullptr)) && period > 0) {
            const UINT32 period_frames = (UINT32) ((period * sample_rate) / 10000000);
            if (period_frames > 0 && period_frames < device_frames) {
                this->reported_frames = period_frames;
                return period_frames;
            }
        }

        this->reported_frames = device_frames;
        return device_frames;
    }

    void SharedRedirect::enable_bridge(int frame_bytes) {
        if (!this->redirected_from_exclusive || frame_bytes <= 0) {
            return;
        }

        this->frame_bytes = frame_bytes;
        this->device_buffer_frames = 0;
        this->fifo.clear();

        log_info("audio::wasapi", "shared-mode buffer bridge enabled (frame size {} bytes)",
                frame_bytes);
    }

    BYTE *SharedRedirect::begin_write(UINT32 frames) {
        // reserve space at the FIFO tail and let the game write straight into it - no scratch copy.
        this->pending_write_offset = this->fifo.size();
        this->fifo.resize(this->pending_write_offset + (size_t) frames * this->frame_bytes);

        return this->fifo.data() + this->pending_write_offset;
    }

    void SharedRedirect::commit_write(UINT32 frames, bool silent) {
        // trim the tail reservation to the frames actually written; zero it in place if silent.
        const size_t end = this->pending_write_offset + (size_t) frames * this->frame_bytes;
        if (silent) {
            std::fill(this->fifo.begin() + this->pending_write_offset,
                    this->fifo.begin() + end, (BYTE) 0);
        }
        this->fifo.resize(end);
    }

    UINT32 SharedRedirect::pending_frames() const {
        if (this->frame_bytes <= 0) {
            return 0;
        }

        return (UINT32) (this->fifo.size() / this->frame_bytes);
    }

    UINT32 SharedRedirect::virtual_padding() const {
        const UINT32 pending = this->pending_frames();
        return this->reported_frames > 0 ? std::min(pending, this->reported_frames) : pending;
    }

    HRESULT SharedRedirect::drain(IAudioRenderClient *real, IAudioClient *client,
            const WAVEFORMATEXTENSIBLE &device_format, float boost) {
        if (!this->bridge_enabled()) {
            return S_OK;
        }

        // cache the real device buffer size once; it is fixed for the life of the stream.
        if (this->device_buffer_frames == 0) {
            if (FAILED(client->GetBufferSize(&this->device_buffer_frames))
                    || this->device_buffer_frames == 0) {
                return S_OK;
            }
        }

        const UINT32 pending = this->pending_frames();
        if (pending == 0) {
            return S_OK;
        }

        // push only as many frames as the device currently has free, keeping the rest queued. this
        // self-paces to the engine's real consumption so a full-buffer write never overflows.
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

        const size_t bytes = (size_t) to_write * this->frame_bytes;
        std::copy(this->fifo.begin(), this->fifo.begin() + bytes, dev);

        // mute the first few buffers to avoid a startup pop, then apply the volume boost.
        if (this->buffers_to_mute > 0) {
            std::fill(dev, dev + bytes, (BYTE) 0);
            this->buffers_to_mute--;
        } else if (boost != 1.0f) {
            apply_gain(dev, to_write, device_format, boost);
        }

        ret = real->ReleaseBuffer(to_write, 0);

        // drop the frames just handed to the device from the front of the FIFO.
        this->fifo.erase(this->fifo.begin(), this->fifo.begin() + bytes);

        return ret;
    }
}
