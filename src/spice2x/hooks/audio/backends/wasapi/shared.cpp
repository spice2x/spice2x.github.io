#include "shared.h"

#include <audioclient.h>

#include "hooks/audio/audio.h"
#include "util/logging.h"

#include "defs.h"

namespace hooks::audio {

    bool SharedRedirect::wants(AUDCLNT_SHAREMODE share_mode) {
        return hooks::audio::WASAPI_COMPATIBILITY_MODE && share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE;
    }

    // whether the engine's PCM converter can handle this format. PCM / IEEE-float only; non-PCM
    // bitstream (e.g. AC-3 / DTS passthrough) must be left alone.
    static bool is_pcm_or_float(const WAVEFORMATEX *format) {
        if (format == nullptr) {
            return false;
        }

        switch (format->wFormatTag) {
            case WAVE_FORMAT_PCM:
            case WAVE_FORMAT_IEEE_FLOAT:
                return true;
            case WAVE_FORMAT_EXTENSIBLE: {

                // SubFormat is only present when the extra-bytes block is large enough
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

    void SharedRedirect::apply(AUDCLNT_SHAREMODE *share_mode, DWORD *stream_flags,
            REFERENCE_TIME *periodicity, const WAVEFORMATEX *format) {

        // shared mode requires periodicity == 0; AUTOCONVERTPCM lets the engine accept the game's
        // native format (without it shared Initialize returns AUDCLNT_E_UNSUPPORTED_FORMAT).
        log_info("audio::wasapi", "redirecting exclusive WASAPI to shared mode");
        *share_mode = AUDCLNT_SHAREMODE_SHARED;
        *periodicity = 0;
        if (is_pcm_or_float(format)) {
            *stream_flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        }
        this->redirected_from_exclusive = true;
    }

    void SharedRedirect::add_compat_converter(DWORD *stream_flags, const WAVEFORMATEX *format) {
        if (is_pcm_or_float(format) && (*stream_flags & AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM) == 0) {

            log_info("audio::wasapi", "adding shared-mode format converter for compatibility");

            // already shared: add the converter so a non-mix-format submission still succeeds (the
            // OS does the sample-rate conversion, so the device settings need not match the game).
            // leave the redirect inactive since the game paces itself against the shared buffer.
            *stream_flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        }
    }

    UINT32 SharedRedirect::clamp_buffer_size(IAudioClient *real, uint32_t sample_rate,
            UINT32 device_frames) const {
        if (!this->redirected_from_exclusive || real == nullptr || sample_rate == 0 || device_frames == 0) {
            return device_frames;
        }

        // GetDevicePeriod returns REFERENCE_TIME units (100 ns), 10^7 per second, so
        // period_frames = period * sample_rate / 10^7.
        REFERENCE_TIME period = 0;
        if (SUCCEEDED(real->GetDevicePeriod(&period, nullptr)) && period > 0) {
            const UINT32 period_frames = (UINT32) ((period * sample_rate) / 10000000);
            if (period_frames > 0 && period_frames < device_frames) {
                return period_frames;
            }
        }

        return device_frames;
    }
}
