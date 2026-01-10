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
// {1FBC8530-AF3E-4128-B418-115DE72F76B8}
static const GUID IID_WrappedIAudioClient3 = {
    0x1fbc8530, 0xaf3e, 0x4128, { 0xb4, 0x18, 0x11, 0x5d, 0xe7, 0x2f, 0x76, 0xb8 }
};

IAudioClient *wrap_audio_client(IAudioClient *client);
IAudioClient3 *wrap_audio_client3(IAudioClient3 *client);

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

struct WrappedIAudioClient3 : IAudioClient3 {

    explicit WrappedIAudioClient3(IAudioClient3 *orig, AudioBackend *backend) : pReal3(orig), backend(backend) {
    }

    WrappedIAudioClient3(const WrappedIAudioClient3 &) = delete;
    WrappedIAudioClient3 &operator=(const WrappedIAudioClient3 &) = delete;
    virtual ~WrappedIAudioClient3() = default;

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

#pragma region IAudioClient2

    HRESULT STDMETHODCALLTYPE IsOffloadCapable(
        AUDIO_STREAM_CATEGORY Category,
        BOOL *pbOffloadCapable) override;

    HRESULT STDMETHODCALLTYPE SetClientProperties( 
        const AudioClientProperties *pProperties) override;

    HRESULT STDMETHODCALLTYPE GetBufferSizeLimits( 
        const WAVEFORMATEX *pFormat,
        BOOL bEventDriven,
        REFERENCE_TIME *phnsMinBufferDuration,
        REFERENCE_TIME *phnsMaxBufferDuration) override;

#pragma endregion

#pragma region IAudioClient3
    HRESULT STDMETHODCALLTYPE GetSharedModeEnginePeriod( 
        const WAVEFORMATEX *pFormat,
        UINT32 *pDefaultPeriodInFrames,
        UINT32 *pFundamentalPeriodInFrames,
        UINT32 *pMinPeriodInFrames,
        UINT32 *pMaxPeriodInFrames) override;
    
    HRESULT STDMETHODCALLTYPE GetCurrentSharedModeEnginePeriod( 
        WAVEFORMATEX **ppFormat,
        UINT32 *pCurrentPeriodInFrames) override;
    
    HRESULT STDMETHODCALLTYPE InitializeSharedAudioStream( 
        DWORD StreamFlags,
        UINT32 PeriodInFrames,
        const WAVEFORMATEX *pFormat,
        LPCGUID AudioSessionGuid) override;
        
#pragma endregion

    IAudioClient3 *const pReal3;
    AudioBackend *const backend;
    bool exclusive_mode = false;
    int frame_size = 0;
};