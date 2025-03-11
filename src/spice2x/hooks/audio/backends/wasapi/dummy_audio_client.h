#pragma once

#include <atomic>
#include <vector>

#include <initguid.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "hooks/audio/implementations/backend.h"
#include "hooks/audio/implementations/asio.h"
#include "hooks/audio/audio_private.h"
#include "util/logging.h"

// {F0842A04-0F8E-4F5C-B3FF-0ED24C589BDA}
static const GUID IID_DummyIAudioClient = {
    0xf0842a04, 0x0f8e, 0x4f5c, { 0xb3, 0xff, 0x0e, 0xd2, 0x4c, 0x58, 0x9b, 0xda }
};

struct DummyIAudioClient : IAudioClient {
    explicit DummyIAudioClient(AudioBackend *backend) : backend(backend) {
        if (!this->backend) {
            log_fatal("audio::wasapi", "DummyIAudioClient: no backend initialized");
        }
    }

    DummyIAudioClient(const DummyIAudioClient &) = delete;
    DummyIAudioClient &operator=(const DummyIAudioClient &) = delete;

    virtual ~DummyIAudioClient() {
        log_misc("audio::wasapi", "~DummyIAudioClient");

        delete this->backend;
    }

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAudioClient
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE ShareMode, DWORD StreamFlags, REFERENCE_TIME hnsBufferDuration, REFERENCE_TIME hnsPeriodicity, const WAVEFORMATEX *pFormat, LPCGUID AudioSessionGuid) override;
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32 *pNumBufferFrames) override;
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME *phnsLatency) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32 *pNumPaddingFrames) override;
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE ShareMode, const WAVEFORMATEX *pFormat, WAVEFORMATEX **ppClosestMatch) override;
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX **ppDeviceFormat) override;
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME *phnsDefaultDevicePeriod, REFERENCE_TIME *phnsMinimumDevicePeriod) override;
    HRESULT STDMETHODCALLTYPE Start() override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Reset() override;
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE eventHandle) override;
    HRESULT STDMETHODCALLTYPE GetService(REFIID riid, void **ppv) override;
#pragma endregion

    std::atomic<ULONG> ref_cnt = 1;

    AudioBackend *const backend;

    std::vector<IAudioSessionEvents *> session_notification_handlers;
};
