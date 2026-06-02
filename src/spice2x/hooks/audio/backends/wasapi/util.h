#pragma once

#include <string>

#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>

std::string stream_flags_str(DWORD flags);

// initialize the real audio client, recovering from AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED by asking the
// device for the next aligned buffer size and re-initializing with a matching duration. log_group
// names the subsystem in the retry log line.
HRESULT initialize_with_alignment_retry(IAudioClient *client, const char *log_group,
        AUDCLNT_SHAREMODE share_mode, DWORD stream_flags, REFERENCE_TIME buffer_duration,
        REFERENCE_TIME periodicity, const WAVEFORMATEX *device_format, LPCGUID session_guid);
