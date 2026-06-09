#pragma once

#include <cstdint>

#include <windows.h>
#include <audioclient.h>

namespace hooks::audio {

    // The -audioshared option redirects an exclusive WASAPI stream to shared mode, and adds the
    // engine's format converter (AUTOCONVERTPCM) to a natively-shared stream for compatibility. This
    // lets other apps play sound at once and works on devices that can't open the exclusive format,
    // at the cost of some latency. Only PCM / float is converted; bitstream (AC-3 / DTS) is left
    // alone.
    struct SharedRedirect {

        // true once apply() has redirected an exclusive request to shared mode. gates the buffer
        // clamp; stays false for a natively-shared stream (it paces itself, so must not be clamped).
        bool redirected_from_exclusive = false;

        // whether an exclusive-mode request should be redirected, given the -audioshared option.
        static bool wants(AUDCLNT_SHAREMODE share_mode);

        // redirect an exclusive request to shared mode. caller must have checked wants() first.
        void apply(AUDCLNT_SHAREMODE *share_mode, DWORD *stream_flags, REFERENCE_TIME *periodicity,
                const WAVEFORMATEX *format);

        // add the engine's format converter to an already-shared stream. PCM / float only, no-op if
        // already requested.
        static void add_compat_converter(DWORD *stream_flags, const WAVEFORMATEX *format);

        // clamp a reported buffer size to one device period. some games write the whole buffer per
        // event, which overflows shared-mode buffering (AUDCLNT_E_BUFFER_TOO_LARGE -> stutter); one
        // period always fits. a no-op unless an exclusive request was redirected.
        UINT32 clamp_buffer_size(IAudioClient *real, uint32_t sample_rate, UINT32 device_frames) const;
    };
}
