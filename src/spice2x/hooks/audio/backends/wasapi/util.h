#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>

std::string stream_flags_str(DWORD flags);

// log the stream parameters (share mode, flags, buffer duration, periodicity) followed by the wave
// format, matching the block printed at the top of IAudioClient::Initialize.
void print_format(AUDCLNT_SHAREMODE share_mode, DWORD stream_flags, REFERENCE_TIME buffer_duration,
        REFERENCE_TIME periodicity, const WAVEFORMATEX *device_format);

// log the share mode followed by the wave format, for paths that only have a share mode (e.g.
// IAudioClient::IsFormatSupported).
void print_format(AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX *device_format);

// detect IEEE float samples: WAVE_FORMAT_IEEE_FLOAT, or WAVE_FORMAT_EXTENSIBLE whose SubFormat is
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT (Data1 == 3; _PCM has Data1 == 1)
inline bool is_ieee_float(const WAVEFORMATEX *fmt) {
    return fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
        || (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE
            && reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(fmt)->SubFormat.Data1 == 0x00000003);
}

// read one sample at `p` as a normalized float in [-1, 1]
inline float read_sample(const BYTE *p, int bytes, bool is_float) {
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
inline void write_sample(BYTE *p, int bytes, bool is_float, float value) {
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

// initialize the real audio client, recovering from AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED by asking the
// device for the next aligned buffer size and re-initializing with a matching duration. log_group
// names the subsystem in the retry log line.
HRESULT initialize_with_alignment_retry(IAudioClient *client, const char *log_group,
        AUDCLNT_SHAREMODE share_mode, DWORD stream_flags, REFERENCE_TIME buffer_duration,
        REFERENCE_TIME periodicity, const WAVEFORMATEX *device_format, LPCGUID session_guid);
