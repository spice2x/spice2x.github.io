#include "util.h"

#include <audioclient.h>

#include "util/flags_helper.h"
#include "util/logging.h"

#include "defs.h"

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

