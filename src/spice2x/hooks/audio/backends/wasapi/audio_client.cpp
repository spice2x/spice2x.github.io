#include "audio_client.h"

#include <ks.h>
#include <ksmedia.h>

#include "avs/game.h"
#include "hooks/audio/audio.h"
#include "hooks/audio/util.h"
#include "hooks/audio/backends/wasapi/util.h"
#include "hooks/audio/implementations/asio.h"
#include "hooks/audio/implementations/wave_out.h"
//#include "util/co_task_mem_ptr.h"
#include "util/utils.h"

#include "defs.h"
#include "dummy_audio_client.h"
#include "wasapi_private.h"

#if 1
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

const char CLASS_NAME[] = "WrappedIAudioClient";

static void fix_rec_format(WAVEFORMATEX *pFormat) {
    log_misc("audio::wasapi", "changing format to 2ch 16-bit");

    pFormat->nChannels = 2;
    pFormat->wBitsPerSample = 16;
    pFormat->nBlockAlign = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
    pFormat->nAvgBytesPerSec = pFormat->nSamplesPerSec * pFormat->nBlockAlign;
}

IAudioClient *wrap_audio_client(IAudioClient *audio_client) {
    AudioBackend *backend = nullptr;
    bool requires_dummy = false;

    if (hooks::audio::BACKEND.has_value()) {
        switch (hooks::audio::BACKEND.value()) {
            case hooks::audio::Backend::Asio:
                backend = new AsioBackend();
                requires_dummy = true;
                break;
            case hooks::audio::Backend::WaveOut:
                backend = new WaveOutBackend();
                break;
            default:
                break;
        }
    }

    IAudioClient *new_client;

    if (hooks::audio::USE_DUMMY || requires_dummy) {

        // release the old context since it is not used by the dummy context
        audio_client->Release();

        new_client = new DummyIAudioClient(backend);
    } else {
        new_client = new WrappedIAudioClient(audio_client, backend);
    }

    return new_client;
}
IAudioClient3 *wrap_audio_client3(IAudioClient3 *audio_client) {
    // TODO: ASIO backend for IAudioClient3?
    log_info("audio::wasapi", "wrapping IAudioClient3 without backend");
    return new WrappedIAudioClient3(audio_client, nullptr);
}

// IUnknown
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIAudioClient || riid == IID_IAudioClient) {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}
ULONG STDMETHODCALLTYPE WrappedIAudioClient::AddRef() {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIAudioClient::Release() {

    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioClient
static HRESULT audioclient_Initialize(
    IAudioClient *pReal,
    AudioBackend *backend,
    bool *exclusive_mode,
    int *frame_size,
    AUDCLNT_SHAREMODE ShareMode,
    DWORD StreamFlags, 
    REFERENCE_TIME hnsBufferDuration,
    REFERENCE_TIME hnsPeriodicity,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) {

    WRAP_DEBUG;

    if (!pFormat) {
        return E_POINTER;
    }

    // check if format needs to be fixed
    if (pFormat->nChannels > 2 && avs::game::is_model("REC")) {
        fix_rec_format(const_cast<WAVEFORMATEX *>(pFormat));
    }

    // verbose output
    log_info("audio::wasapi", "IAudioClient::Initialize hook hit");
    log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(ShareMode));
    log_info("audio::wasapi", "... StreamFlags       : {}", stream_flags_str(StreamFlags));
    log_info("audio::wasapi", "... hnsBufferDuration : {}", hnsBufferDuration);
    log_info("audio::wasapi", "... hnsPeriodicity    : {}", hnsPeriodicity);
    print_format(pFormat);

    if (backend) {
        SAFE_CALL("AudioBackend", "on_initialize", backend->on_initialize(
                &ShareMode,
                &StreamFlags,
                &hnsBufferDuration,
                &hnsPeriodicity,
                pFormat,
                AudioSessionGuid));

        log_info("audio::wasapi", "AudioBackend::on_initialize call finished");
        log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(ShareMode));
        log_info("audio::wasapi", "... StreamFlags       : {}", stream_flags_str(StreamFlags));
        log_info("audio::wasapi", "... hnsBufferDuration : {}", hnsBufferDuration);
        log_info("audio::wasapi", "... hnsPeriodicity    : {}", hnsPeriodicity);
        print_format(pFormat);
    }

    // check for exclusive mode
    if (ShareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
        *exclusive_mode = true;
        *frame_size = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
    }

    // call next
    HRESULT ret = pReal->Initialize(
            ShareMode,
            StreamFlags,
            hnsBufferDuration,
            hnsPeriodicity,
            pFormat,
            AudioSessionGuid);

    // check for failure
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient", "Initialize", ret);
        return ret;
    }

    log_info("audio::wasapi", "IAudioClient::Initialize success, hr={}", FMT_HRESULT(ret));
    copy_wave_format(&hooks::audio::FORMAT, pFormat);

    return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Initialize(
    AUDCLNT_SHAREMODE ShareMode,
    DWORD StreamFlags,
    REFERENCE_TIME hnsBufferDuration,
    REFERENCE_TIME hnsPeriodicity,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) {

    return audioclient_Initialize(
        this->pReal,
        this->backend,
        &this->exclusive_mode,
        &this->frame_size,
        ShareMode,
        StreamFlags,
        hnsBufferDuration,
        hnsPeriodicity,
        pFormat,
        AudioSessionGuid);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::Initialize(
    AUDCLNT_SHAREMODE ShareMode,
    DWORD StreamFlags,
    REFERENCE_TIME hnsBufferDuration,
    REFERENCE_TIME hnsPeriodicity,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) {

    return audioclient_Initialize(
        this->pReal3,
        this->backend,
        &this->exclusive_mode,
        &this->frame_size,
        ShareMode,
        StreamFlags,
        hnsBufferDuration,
        hnsPeriodicity,
        pFormat,
        AudioSessionGuid);
}

static HRESULT audioclient_GetBufferSize(
    IAudioClient *pReal,
    AudioBackend *backend,
    UINT32 *pNumBufferFrames) {

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetBufferSize");
    });

    if (backend) {
        uint32_t buffer_frames = 0;

        SAFE_CALL("AudioBackend", "on_get_buffer_size", backend->on_get_buffer_size(&buffer_frames));

        if (buffer_frames > 0) {
            *pNumBufferFrames = buffer_frames;

            return S_OK;
        }
    }

    CHECK_RESULT(pReal->GetBufferSize(pNumBufferFrames));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetBufferSize(UINT32 *pNumBufferFrames) {
    return audioclient_GetBufferSize(this->pReal, this->backend, pNumBufferFrames);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetBufferSize(UINT32 *pNumBufferFrames) {
    return audioclient_GetBufferSize(this->pReal3, this->backend, pNumBufferFrames);
}

static HRESULT audioclient_GetStreamLatency(
    IAudioClient *pReal,
    AudioBackend *backend,
    REFERENCE_TIME *phnsLatency) {

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetStreamLatency");
    });

    if (backend) {
        REFERENCE_TIME latency = 0;

        SAFE_CALL("AudioBackend", "on_get_stream_latency", backend->on_get_stream_latency(
                &latency));

        if (latency > 0) {
            *phnsLatency = latency;

            return S_OK;
        }
    }

    CHECK_RESULT(pReal->GetStreamLatency(phnsLatency));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetStreamLatency(REFERENCE_TIME *phnsLatency) {
    return audioclient_GetStreamLatency(this->pReal, this->backend, phnsLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetStreamLatency(REFERENCE_TIME *phnsLatency) {
    return audioclient_GetStreamLatency(this->pReal3, this->backend, phnsLatency);
}


static HRESULT audioclient_GetCurrentPadding(
    IAudioClient *pReal,
    AudioBackend *backend,
    UINT32 *pNumPaddingFrames) {

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetCurrentPadding");
    });

    if (pNumPaddingFrames && backend) {
        std::optional<uint32_t> padding_frames;

        SAFE_CALL("AudioBackend", "on_get_current_padding",backend->on_get_current_padding(
                padding_frames));

        if (padding_frames.has_value()) {
            *pNumPaddingFrames = padding_frames.value();

            return S_OK;
        }
    }

    CHECK_RESULT(pReal->GetCurrentPadding(pNumPaddingFrames));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetCurrentPadding(UINT32 *pNumPaddingFrames) {
    return audioclient_GetCurrentPadding(this->pReal, this->backend, pNumPaddingFrames);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetCurrentPadding(UINT32 *pNumPaddingFrames) {
    return audioclient_GetCurrentPadding(this->pReal3, this->backend, pNumPaddingFrames);
}

static HRESULT audioclient_IsFormatSupported(
    IAudioClient *pReal,
    AudioBackend *backend,
    AUDCLNT_SHAREMODE ShareMode,
    const WAVEFORMATEX *pFormat,
    WAVEFORMATEX **ppClosestMatch)
{
    WRAP_VERBOSE;

    if (!pFormat) {
        return E_POINTER;
    }

    // check if format needs to be fixed
    if (avs::game::is_model("REC") && pFormat->nChannels > 2) {
        fix_rec_format(const_cast<WAVEFORMATEX *>(pFormat));
    }

    if (backend) {
        HRESULT ret = backend->on_is_format_supported(&ShareMode, pFormat, ppClosestMatch);

        if (SUCCEEDED(ret)) {
            return ret;
        }

        // return errors other than unsupported format
        if (ret != AUDCLNT_E_UNSUPPORTED_FORMAT) {
            SAFE_CALL("AudioBackend", "on_is_format_supported", ret);
        }
    }

    CHECK_RESULT(pReal->IsFormatSupported(ShareMode, pFormat, ppClosestMatch));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::IsFormatSupported(
    AUDCLNT_SHAREMODE ShareMode,
    const WAVEFORMATEX *pFormat,
    WAVEFORMATEX **ppClosestMatch) {

    return audioclient_IsFormatSupported(
        this->pReal,
        this->backend,
        ShareMode,
        pFormat,
        ppClosestMatch);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::IsFormatSupported(
    AUDCLNT_SHAREMODE ShareMode,
    const WAVEFORMATEX *pFormat,
    WAVEFORMATEX **ppClosestMatch) {

    return audioclient_IsFormatSupported(
        this->pReal3,
        this->backend,
        ShareMode,
        pFormat,
        ppClosestMatch);
}

static HRESULT audioclient_GetMixFormat(
    IAudioClient *pReal,
    AudioBackend *backend,
    WAVEFORMATEX **ppDeviceFormat) {

    WRAP_VERBOSE;

    if (!ppDeviceFormat) {
        return E_POINTER;
    }

    if (backend) {
        HRESULT ret = backend->on_get_mix_format(ppDeviceFormat);

        if (SUCCEEDED(ret)) {
            return ret;
        }

        // return errors other than E_NOTIMPL
        if (ret != E_NOTIMPL) {
            SAFE_CALL("AudioBackend", "on_get_mix_format", ret);
        }
    }

    CHECK_RESULT(pReal->GetMixFormat(ppDeviceFormat));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetMixFormat(WAVEFORMATEX **ppDeviceFormat) {
    return audioclient_GetMixFormat(this->pReal, this->backend, ppDeviceFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetMixFormat(WAVEFORMATEX **ppDeviceFormat) {
    return audioclient_GetMixFormat(this->pReal3, this->backend, ppDeviceFormat);
}


static HRESULT audioclient_GetDevicePeriod(
    IAudioClient *pReal,
    AudioBackend *backend,
    REFERENCE_TIME *phnsDefaultDevicePeriod,
    REFERENCE_TIME *phnsMinimumDevicePeriod)
{
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetDevicePeriod");
    });

    HRESULT ret = pReal->GetDevicePeriod(phnsDefaultDevicePeriod, phnsMinimumDevicePeriod);

    if (SUCCEEDED(ret) && backend) {
        SAFE_CALL("AudioBackend", "on_get_device_period", backend->on_get_device_period(
                phnsDefaultDevicePeriod,
                phnsMinimumDevicePeriod));
    }

    CHECK_RESULT(ret);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetDevicePeriod(
    REFERENCE_TIME *phnsDefaultDevicePeriod,
    REFERENCE_TIME *phnsMinimumDevicePeriod) {

    return audioclient_GetDevicePeriod(
        this->pReal,
        this->backend,
        phnsDefaultDevicePeriod,
        phnsMinimumDevicePeriod);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetDevicePeriod(
    REFERENCE_TIME *phnsDefaultDevicePeriod,
    REFERENCE_TIME *phnsMinimumDevicePeriod) {

    return audioclient_GetDevicePeriod(
        this->pReal3,
        this->backend,
        phnsDefaultDevicePeriod,
        phnsMinimumDevicePeriod);
}

static HRESULT audioclient_Start(
    IAudioClient *pReal,
    AudioBackend *backend) {

    WRAP_VERBOSE;

    HRESULT ret = pReal->Start();

    if (SUCCEEDED(ret) && backend) {
        SAFE_CALL("AudioBackend", "on_start", backend->on_start());
    }

    CHECK_RESULT(ret);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Start() {
    return audioclient_Start(this->pReal, this->backend);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::Start() {
    return audioclient_Start(this->pReal3, this->backend);
}

static HRESULT audioclient_Stop(
    IAudioClient *pReal,
    AudioBackend *backend) {

    WRAP_VERBOSE;

    HRESULT ret = pReal->Stop();

    if (SUCCEEDED(ret) && backend) {
        SAFE_CALL("AudioBackend", "on_stop", backend->on_stop());
    }

    CHECK_RESULT(ret);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Stop() {
    return audioclient_Stop(this->pReal, this->backend);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::Stop() {
    return audioclient_Stop(this->pReal3, this->backend);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Reset() {
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->Reset());
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::Reset() {
    WRAP_VERBOSE;
    CHECK_RESULT(pReal3->Reset());
}

static HRESULT audioclient_SetEventHandle(
    IAudioClient *pReal,
    AudioBackend *backend,
    HANDLE eventHandle) {

    WRAP_VERBOSE;
    if (backend) {
        SAFE_CALL("AudioBackend", "on_set_event_handle", backend->on_set_event_handle(&eventHandle));
    }

    CHECK_RESULT(pReal->SetEventHandle(eventHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::SetEventHandle(HANDLE eventHandle) {
    return audioclient_SetEventHandle(this->pReal, this->backend, eventHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::SetEventHandle(HANDLE eventHandle) {
    return audioclient_SetEventHandle(this->pReal3, this->backend, eventHandle);
}


HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetService(REFIID riid, void **ppv) {
    WRAP_DEBUG_FMT("WrappedIAudioClient::GetService({})", guid2s(riid));

    HRESULT ret = pReal->GetService(riid, ppv);

    if (SUCCEEDED(ret) && ppv && *ppv && riid == IID_IAudioRenderClient) {
        auto render_client = reinterpret_cast<IAudioRenderClient *>(*ppv);

        *ppv = new WrappedIAudioRenderClient(this, render_client);
    }

    return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetService(REFIID riid, void **ppv) {
    WRAP_DEBUG_FMT("WrappedIAudioClient3::GetService({})", guid2s(riid));
    // TODO: proper backend with WrappedIAudioRenderClient
    return pReal3->GetService(riid, ppv);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIAudioClient3 || riid == IID_IAudioClient) {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal3->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIAudioClient3::AddRef() {
    return pReal3->AddRef();
}

ULONG STDMETHODCALLTYPE WrappedIAudioClient3::Release() {
    // get reference count of underlying interface
    ULONG refs = pReal3 != nullptr ? pReal3->Release() : 0;
    if (refs == 0) {
        delete this;
    }

    return refs;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::IsOffloadCapable(
        AUDIO_STREAM_CATEGORY Category,
        BOOL *pbOffloadCapable) {
    WRAP_VERBOSE;

    CHECK_RESULT(pReal3->IsOffloadCapable(Category, pbOffloadCapable));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::SetClientProperties( 
    const AudioClientProperties *pProperties) {

    WRAP_VERBOSE;
    CHECK_RESULT(pReal3 ->SetClientProperties(pProperties));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetBufferSizeLimits( 
    const WAVEFORMATEX *pFormat,
    BOOL bEventDriven,
    REFERENCE_TIME *phnsMinBufferDuration,
    REFERENCE_TIME *phnsMaxBufferDuration) {

    WRAP_VERBOSE;
    CHECK_RESULT(pReal3->GetBufferSizeLimits(
        pFormat,
        bEventDriven,
        phnsMinBufferDuration,
        phnsMaxBufferDuration));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetSharedModeEnginePeriod( 
    const WAVEFORMATEX *pFormat,
    UINT32 *pDefaultPeriodInFrames,
    UINT32 *pFundamentalPeriodInFrames,
    UINT32 *pMinPeriodInFrames,
    UINT32 *pMaxPeriodInFrames) {

    WRAP_VERBOSE;
    CHECK_RESULT(pReal3->GetSharedModeEnginePeriod(
        pFormat,
        pDefaultPeriodInFrames,
        pFundamentalPeriodInFrames,
        pMinPeriodInFrames,
        pMaxPeriodInFrames));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::GetCurrentSharedModeEnginePeriod( 
    WAVEFORMATEX **ppFormat,
    UINT32 *pCurrentPeriodInFrames) {

    WRAP_VERBOSE;
    CHECK_RESULT(pReal3->GetCurrentSharedModeEnginePeriod(
        ppFormat,
        pCurrentPeriodInFrames));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient3::InitializeSharedAudioStream( 
    DWORD StreamFlags,
    UINT32 PeriodInFrames,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) {

    if (!pFormat) {
        return E_POINTER;
    }

    // verbose output
    log_info("audio::wasapi", "IAudioClien3t::InitializeSharedAudioStream hook hit");
    log_info("audio::wasapi", "... StreamFlags       : {}", stream_flags_str(StreamFlags));
    log_info("audio::wasapi", "... PeriodInFrames    : {}", PeriodInFrames);
    print_format(pFormat);

    // call next
    HRESULT ret = pReal3->InitializeSharedAudioStream(
        StreamFlags,
        PeriodInFrames,
        pFormat,
        AudioSessionGuid);

    // check for failure
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient3", "InitializeSharedAudioStream", ret);
        return ret;
    }

    log_info("audio::wasapi", "IAudioClient3::InitializeSharedAudioStream success, hr={}", FMT_HRESULT(ret));
    copy_wave_format(&hooks::audio::FORMAT, pFormat);
    return ret;
}
