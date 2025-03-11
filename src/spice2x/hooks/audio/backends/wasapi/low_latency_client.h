#pragma once

#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include "hooks/audio/audio.h"

namespace hooks::audio {
    void init_low_latency();
    void stop_low_latency();

    class LowLatencyAudioClient {
    public:
        static LowLatencyAudioClient *Create(IMMDevice *device);
        ~LowLatencyAudioClient();

    private:
        LowLatencyAudioClient(IAudioClient3* audioClient);
        IAudioClient3* audioClient;
    };
}
