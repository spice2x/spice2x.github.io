#include "dummy_audio_client.h"

#include "hooks/audio/audio.h"
#include "hooks/audio/util.h"

#include "defs.h"
#include "dummy_audio_clock.h"
#include "dummy_audio_render_client.h"
#include "dummy_audio_session_control.h"
#include "util.h"
#include "wasapi_private.h"

#if 0
#define WRAP_DEBUG log_misc("audio::wasapi", "{}::{}", CLASS_NAME, __func__)
#define WRAP_DEBUG_FMT(format, ...) log_misc("audio::wasapi", format, __VA_ARGS__)
#else
#define WRAP_DEBUG do {} while (0)
#define WRAP_DEBUG_FMT(format, ...) do {} while (0)
#endif

#if 1
#define WRAP_VERBOSE log_misc("audio::wasapi", "{}::{}", CLASS_NAME, __func__)
#else
#define WRAP_VERBOSE do {} while (0)
#endif

const char CLASS_NAME[] = "DummyIAudioClient";

// IUnknown
HRESULT STDMETHODCALLTYPE DummyIAudioClient::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_DummyIAudioClient ||
        riid == IID_IAudioClient)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE DummyIAudioClient::AddRef() {
    return ++this->ref_cnt;
}
ULONG STDMETHODCALLTYPE DummyIAudioClient::Release() {
    ULONG refs = --this->ref_cnt;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioClient
HRESULT STDMETHODCALLTYPE DummyIAudioClient::Initialize(
    AUDCLNT_SHAREMODE ShareMode,
    DWORD StreamFlags,
    REFERENCE_TIME hnsBufferDuration,
    REFERENCE_TIME hnsPeriodicity,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid)
{
    WRAP_DEBUG;

    if (!pFormat) {
        return E_POINTER;
    }

    // verbose output
    log_info("audio::wasapi", "IAudioClient::Initialize hook hit");
    log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(ShareMode));
    log_info("audio::wasapi", "... StreamFlags       : {}", stream_flags_str(StreamFlags));
    log_info("audio::wasapi", "... hnsBufferDuration : {}", hnsBufferDuration);
    log_info("audio::wasapi", "... hnsPeriodicity    : {}", hnsPeriodicity);
    print_format(pFormat);

    CHECK_RESULT(this->backend->on_initialize(
        &ShareMode,
        &StreamFlags,
        &hnsBufferDuration,
        &hnsPeriodicity,
        pFormat,
        AudioSessionGuid));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::GetBufferSize(UINT32 *pNumBufferFrames) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioClient::GetBufferSize");
    });

    CHECK_RESULT(this->backend->on_get_buffer_size(pNumBufferFrames));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::GetStreamLatency(REFERENCE_TIME *phnsLatency) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioClient::GetStreamLatency");
    });

    CHECK_RESULT(this->backend->on_get_stream_latency(phnsLatency));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::GetCurrentPadding(UINT32 *pNumPaddingFrames) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioClient::GetCurrentPadding");
    });

    if (!pNumPaddingFrames) {
        return E_POINTER;
    }

    std::optional<uint32_t> padding_frames;

    HRESULT ret = this->backend->on_get_current_padding(padding_frames);

    if (SUCCEEDED(ret)) {
        *pNumPaddingFrames = padding_frames.value_or(0);
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::IsFormatSupported(
    AUDCLNT_SHAREMODE ShareMode,
    const WAVEFORMATEX *pFormat,
    WAVEFORMATEX **ppClosestMatch)
{
    WRAP_VERBOSE;

    if (!pFormat) {
        return E_POINTER;
    }

    CHECK_RESULT(this->backend->on_is_format_supported(&ShareMode, pFormat, ppClosestMatch));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::GetMixFormat(WAVEFORMATEX **ppDeviceFormat) {
    WRAP_VERBOSE;

    if (!ppDeviceFormat) {
        return E_POINTER;
    }

    CHECK_RESULT(this->backend->on_get_mix_format(ppDeviceFormat));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::GetDevicePeriod(
    REFERENCE_TIME *phnsDefaultDevicePeriod,
    REFERENCE_TIME *phnsMinimumDevicePeriod)
{
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioClient::GetDevicePeriod");
    });

    CHECK_RESULT(this->backend->on_get_device_period(
        phnsDefaultDevicePeriod,
        phnsMinimumDevicePeriod));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::Start() {
    WRAP_VERBOSE;

    HRESULT ret = this->backend->on_start();

    if (SUCCEEDED(ret)) {
        for (auto &handler : this->session_notification_handlers) {
            handler->OnStateChanged(AudioSessionStateActive);
        }
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::Stop() {
    WRAP_VERBOSE;

    HRESULT ret = this->backend->on_stop();

    if (SUCCEEDED(ret)) {
        for (auto &handler : this->session_notification_handlers) {
            handler->OnStateChanged(AudioSessionStateInactive);
        }
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::Reset() {
    WRAP_VERBOSE;

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::SetEventHandle(HANDLE eventHandle) {
    WRAP_VERBOSE;
    CHECK_RESULT(this->backend->on_set_event_handle(&eventHandle));
}
HRESULT STDMETHODCALLTYPE DummyIAudioClient::GetService(REFIID riid, void **ppv) {
    WRAP_DEBUG_FMT("DummyIAudioClient::GetService({})", guid2s(riid));

    if (ppv) {
        if (riid == IID_IAudioRenderClient) {
            *ppv = new DummyIAudioRenderClient(this);

            return S_OK;
        } else if (riid == IID_IAudioSessionControl) {
            *ppv = new DummyIAudioSessionControl(this);

            return S_OK;
        } else if (riid == IID_IAudioClock) {
            *ppv = new DummyIAudioClock(this->backend);

            return S_OK;
        }
    }

    CHECK_RESULT(E_NOINTERFACE);
}
