#include "util.h"

#include <audioclient.h>

#include "hooks/audio/util.h"
#include "util/flags_helper.h"
#include "util/logging.h"

#include "defs.h"

void apply_gain(BYTE *buffer, UINT32 frames, const WAVEFORMATEXTENSIBLE &fmt, float gain) {
    const WAVEFORMATEX &f = fmt.Format;
    const size_t samples = (size_t) frames * f.nChannels;

    bool is_float = is_ieee_float(&f);

    if (is_float && f.wBitsPerSample == 32) {
        auto p = reinterpret_cast<float *>(buffer);
        for (size_t i = 0; i < samples; i++) {
            p[i] = std::clamp(p[i] * gain, -1.0f, 1.0f);
        }
        return;
    }

    switch (f.wBitsPerSample) {
        case 16: {
            auto p = reinterpret_cast<int16_t *>(buffer);
            for (size_t i = 0; i < samples; i++) {
                p[i] = (int16_t) std::clamp((int) std::lround(p[i] * gain), -32768, 32767);
            }
            break;
        }
        case 24: {
            // packed 24-bit little-endian
            for (size_t i = 0; i < samples; i++) {
                BYTE *s = buffer + i * 3;
                int32_t v = s[0] | (s[1] << 8) | (s[2] << 16);
                if (v & 0x800000) {
                    v |= ~0xFFFFFF; // sign extend
                }
                int64_t scaled = std::clamp<int64_t>(
                    std::llround((double) v * gain), -8388608, 8388607);
                s[0] = scaled & 0xFF;
                s[1] = (scaled >> 8) & 0xFF;
                s[2] = (scaled >> 16) & 0xFF;
            }
            break;
        }
        case 32: {
            auto p = reinterpret_cast<int32_t *>(buffer);
            for (size_t i = 0; i < samples; i++) {
                p[i] = (int32_t) std::clamp(
                    std::llround((double) p[i] * gain),
                    (long long) INT32_MIN, (long long) INT32_MAX);
            }
            break;
        }
        default:
            break;
    }
}

std::string stream_flags_str(DWORD flags) {
    FLAGS_START(flags);
    FLAG(flags, AUDCLNT_STREAMFLAGS_CROSSPROCESS);
    FLAG(flags, AUDCLNT_STREAMFLAGS_LOOPBACK);
    FLAG(flags, AUDCLNT_STREAMFLAGS_EVENTCALLBACK);
    FLAG(flags, AUDCLNT_STREAMFLAGS_NOPERSIST);
    FLAG(flags, AUDCLNT_STREAMFLAGS_RATEADJUST);
    FLAG(flags, AUDCLNT_STREAMFLAGS_PREVENT_LOOPBACK_CAPTURE);
    FLAG(flags, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM);
    FLAG(flags, AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    FLAGS_END(flags);
}

void print_format(AUDCLNT_SHAREMODE share_mode, DWORD stream_flags, REFERENCE_TIME buffer_duration,
        REFERENCE_TIME periodicity, const WAVEFORMATEX *device_format) {
    log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(share_mode));
    log_info("audio::wasapi", "... StreamFlags       : {}", stream_flags_str(stream_flags));
    log_info("audio::wasapi", "... hnsBufferDuration : {} ({:.3f} ms)",
            buffer_duration, buffer_duration / 10000.0);
    log_info("audio::wasapi", "... hnsPeriodicity    : {} ({:.3f} ms)",
            periodicity, periodicity / 10000.0);
    print_format(device_format);
}

void print_format(AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX *device_format) {
    log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(share_mode));
    print_format(device_format);
}

HRESULT initialize_with_alignment_retry(IAudioClient *client, const char *log_group,
        AUDCLNT_SHAREMODE share_mode, DWORD stream_flags, REFERENCE_TIME buffer_duration,
        REFERENCE_TIME periodicity, const WAVEFORMATEX *device_format, LPCGUID session_guid) {

    HRESULT ret = client->Initialize(share_mode, stream_flags, buffer_duration, periodicity,
            device_format, session_guid);

    // the requested buffer size can end up unaligned for the device; recover by asking for the next
    // aligned buffer size and re-initializing with a matching duration.
    if (ret == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        UINT32 aligned_frames = 0;
        if (SUCCEEDED(client->GetBufferSize(&aligned_frames)) && aligned_frames > 0) {
            REFERENCE_TIME aligned_duration = (REFERENCE_TIME)
                    (10000.0 * 1000 / device_format->nSamplesPerSec * aligned_frames + 0.5);

            log_info(log_group, "buffer not aligned, retrying with {} frames ({} hns)",
                    aligned_frames, aligned_duration);

            ret = client->Initialize(share_mode, stream_flags, aligned_duration,
                    periodicity != 0 ? aligned_duration : 0, device_format, session_guid);
        }
    }

    return ret;
}

