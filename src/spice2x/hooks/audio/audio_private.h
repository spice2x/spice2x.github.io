#pragma once

#include <mutex>

#include <windows.h>
#include <audioclient.h>
#include "hooks/audio/backends/wasapi/low_latency_client.h"

constexpr bool AUDIO_LOG_HRESULT = true;

namespace hooks::audio {

    extern IAudioClient *CLIENT;
    extern std::mutex INITIALIZE_LOCK;
    extern bool VOLUME_HOOK_ENABLED;
    extern LowLatencyAudioClient *LOW_LATENCY_CLIENT;
}
