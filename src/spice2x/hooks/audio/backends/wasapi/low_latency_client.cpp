#include "low_latency_client.h"
#include "util/logging.h"

#ifdef _MSC_VER
DEFINE_GUID(CLSID_MMDeviceEnumerator,
    0xBCDE0395, 0xE52F, 0x467C,
    0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
#endif

#define PRINT_FAILED_RESULT(name, ret) \
    log_warning("audio::lowlatency", "{} failed, hr={}", name, FMT_HRESULT(ret))

namespace hooks::audio {
    bool LOW_LATENCY_SHARED_WASAPI = false;
    static bool COM_INITIALIZED = false;
    static LowLatencyAudioClient *LOW_LATENCY_CLIENT = nullptr;

    void init_low_latency() {
        if (!LOW_LATENCY_SHARED_WASAPI) {
            return;
        }
        log_info("audio::lowlatency", "initializing");

        HRESULT hr;

        // initialize COM
        COM_INITIALIZED = true;
        hr = CoInitialize(NULL);
        if (FAILED(hr)) {
            PRINT_FAILED_RESULT("CoInitialize", hr);
            return;
        }

        // initialize device enumerator
        IMMDeviceEnumerator* enumerator;
        hr = CoCreateInstance(
                CLSID_MMDeviceEnumerator,
                NULL,
                CLSCTX_ALL,
                IID_IMMDeviceEnumerator,
                reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) {
            PRINT_FAILED_RESULT("CoCreateInstance(CLSID_MMDeviceEnumerator)", hr);
            return;
        }

        // get default audio endpoint from enumerator
        IMMDevice* device;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) {
            PRINT_FAILED_RESULT("GetDefaultAudioEndpoint", hr);
            return;
        }
        enumerator->Release();

        // start the client using the default audio endpoint
        LOW_LATENCY_CLIENT = hooks::audio::LowLatencyAudioClient::Create(device);
        log_info("audio::lowlatency", "initialized");
    }

    void stop_low_latency() {
        if (!LOW_LATENCY_SHARED_WASAPI) {
            return;
        }
        log_info("audio::lowlatency", "stopping");
        if (LOW_LATENCY_CLIENT) {
            delete LOW_LATENCY_CLIENT;
            LOW_LATENCY_CLIENT = nullptr;
        }
        if (COM_INITIALIZED) {
            COM_INITIALIZED = false;
            CoUninitialize();
        }
        log_info("audio::lowlatency", "stopped");
    }
}

using namespace hooks::audio;

LowLatencyAudioClient::LowLatencyAudioClient(IAudioClient3* audioClient) : audioClient(audioClient) {}

LowLatencyAudioClient::~LowLatencyAudioClient() {
    if (this->audioClient) {
        HRESULT ret;
        ret = this->audioClient->Stop();
        if (FAILED(ret)) {
            PRINT_FAILED_RESULT("IAudioClient3::Stop", ret);
        }
        this->audioClient->Release();
        this->audioClient = nullptr;
    }
}

LowLatencyAudioClient *LowLatencyAudioClient::Create(IMMDevice *device) {
    HRESULT ret;
    UINT32 minPeriod;
    UINT32 defaultPeriod;
    UINT32 fundamentalPeriod;
    UINT32 maxPeriod;
    PWAVEFORMATEX pFormat;
    IAudioClient3* audioClient;

    ret = device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, NULL, reinterpret_cast<void**>(&audioClient));
    device->Release();
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IMMDevice::Activate(IID_IAudioClient3...)", ret);
        log_warning("audio::lowlatency", "note that only Windows 10 and above supports IAudioClient3");
        return nullptr;
    }

    ret = audioClient->GetMixFormat(&pFormat);
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient3::GetMixFormat", ret);
        audioClient->Release();
        audioClient = nullptr;
        return nullptr;
    }

    ret = audioClient->GetSharedModeEnginePeriod(pFormat, &defaultPeriod, &fundamentalPeriod, &minPeriod, &maxPeriod);
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient3::GetSharedModeEnginePeriod", ret);
        audioClient->Release();
        audioClient = nullptr;
        return nullptr;
    }

    ret = audioClient->InitializeSharedAudioStream(0, minPeriod, pFormat, NULL);
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient3::InitializeSharedAudioStream", ret);
        audioClient->Release();
        audioClient = nullptr;
        return nullptr;
    }

    ret = audioClient->Start();
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient3::Start", ret);
        audioClient->Release();
        audioClient = nullptr;
        return nullptr;
    }

    log_info("audio::lowlatency", "low latency shared mode audio client initialized successfully.");
    log_info("audio::lowlatency", "this is NOT used to output sound...");
    log_info("audio::lowlatency", "but rather to reduce buffer sizes when the game requests an audio client at a later point");
    log_info("audio::lowlatency", "has no effect if the game uses exclusive WASAPI or ASIO!");
    log_info("audio::lowlatency", "... sample rate         : {} Hz", pFormat->nSamplesPerSec);
    log_info("audio::lowlatency", "... min buffer size     : {} samples ({} ms)", minPeriod, 1000.0f * minPeriod / pFormat->nSamplesPerSec);
    log_info("audio::lowlatency", "... max buffer size     : {} samples ({} ms)", maxPeriod, 1000.0f * maxPeriod / pFormat->nSamplesPerSec);
    log_info("audio::lowlatency", "... default buffer size : {} samples ({} ms)", defaultPeriod, 1000.0f * defaultPeriod / pFormat->nSamplesPerSec);
    log_info("audio::lowlatency", "... Windows will use minimum buffer size (instead of default) for shared mode audio clients from now on");
    return new LowLatencyAudioClient(audioClient);
}
