#pragma once

#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include "hooks/audio/implementations/backend.h"
#include "hooks/audio/audio_private.h"
#include "util/logging.h"

#include "audio_render_client.h"

// {1FBC8530-AF3E-4128-B418-115DE72F76B6}
static const GUID IID_WrappedIAudioClient = {
    0x1fbc8530, 0xaf3e, 0x4128, { 0xb4, 0x18, 0x11, 0x5d, 0xe7, 0x2f, 0x76, 0xb6 }
};

IAudioClient *wrap_audio_client(IAudioClient *client);

struct WrappedIAudioClient : IAudioClient {
    explicit WrappedIAudioClient(IAudioClient *orig, AudioBackend *backend) : pReal(orig), backend(backend) {
    }

    WrappedIAudioClient(const WrappedIAudioClient &) = delete;
    WrappedIAudioClient &operator=(const WrappedIAudioClient &) = delete;

    virtual ~WrappedIAudioClient() = default;

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

    IAudioClient *const pReal;
    AudioBackend *const backend;

    bool exclusive_mode = false;
    int frame_size = 0;
};
