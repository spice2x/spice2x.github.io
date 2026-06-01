#include "audio_client.h"

#include <ks.h>
#include <ksmedia.h>

#include "avs/game.h"
#include "games/gitadora/gitadora.h"
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

const char CLASS_NAME[] = "WrappedIAudioClient";

static void fix_rec_format(WAVEFORMATEX *pFormat) {
    log_misc("audio::wasapi", "changing format to 2ch 16-bit");

    pFormat->nChannels = 2;
    pFormat->wBitsPerSample = 16;
    pFormat->nBlockAlign = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
    pFormat->nAvgBytesPerSec = pFormat->nSamplesPerSec * pFormat->nBlockAlign;
}

// decide whether the given multi-channel format should be downmixed to stereo and which algorithm
// to use. an explicit user selection (-downmix) takes precedence; otherwise gitadora arena
// two-channel mode defaults to the AC-4 algorithm.
static std::optional<hooks::audio::DownmixAlgorithm> resolve_downmix(const WAVEFORMATEX *format) {
    if (format == nullptr
            || format->nChannels <= 2
            || format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
        return std::nullopt;
    }

    if (hooks::audio::DOWNMIX_ALGORITHM.has_value()) {
        return hooks::audio::DOWNMIX_ALGORITHM;
    }

    if (games::gitadora::is_arena_model() && games::gitadora::TWOCHANNEL) {
        return hooks::audio::DownmixAlgorithm::AC4;
    }

    return std::nullopt;
}

IAudioClient *wrap_audio_client(IAudioClient *audio_client) {
    log_misc("audio::wasapi", "wrapping IAudioClient");

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
    // TODO: ASIO backend for IAudioClient3, if there is a game that needs it
    log_misc("audio::wasapi", "wrapping IAudioClient3");

    if (hooks::audio::BACKEND.has_value()) {
        log_fatal(
            "audio::wasapi",
            "IAudioClient3 does not currently support backends! clear -audiobackend and try again");
    }
    if (hooks::audio::USE_DUMMY) {
        log_fatal(
            "audio::wasapi",
            "IAudioClient3 does not currently support dummy context, clear -audiodummy and try again");
    }

    return new WrappedIAudioClient(audio_client, nullptr);
}

// IUnknown
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIAudioClient ||
        riid == IID_IAudioClient ||
        riid == IID_IAudioClient3) {
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
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Initialize(
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

    // check if format needs to be fixed
    if (pFormat->nChannels > 2 && avs::game::is_model("REC")) {
        fix_rec_format(const_cast<WAVEFORMATEX *>(pFormat));
    }

    // when downmixing, open the real device as stereo while the game keeps writing its native
    // multi-channel format into the scratch buffer.
    WAVEFORMATEXTENSIBLE stereo_storage = {};
    const WAVEFORMATEX *device_format = pFormat;

    if (auto algorithm = resolve_downmix(pFormat)) {
        this->downmix.setup(pFormat, &stereo_storage, *algorithm);
        device_format = reinterpret_cast<const WAVEFORMATEX *>(&stereo_storage);
        log_info("audio::wasapi", "downmix enabled: {} channels -> 2 channels ({})",
                pFormat->nChannels, hooks::audio::Downmix::algorithm_name(*algorithm));
    } else if (games::gitadora::is_arena_model()) {
        games::gitadora::fix_audio_channel_mask(const_cast<WAVEFORMATEX *>(pFormat));
    }

    // when resampling, open the real device at the target rate while the game keeps writing its
    // native-rate audio into the scratch buffer. this runs on whatever device_format is now: the
    // game's native format, or the stereo format produced above when downmix is also active, so
    // the two stages chain as multi-channel -> stereo -> resampled stereo.
    WAVEFORMATEXTENSIBLE resample_storage = {};
    if (auto target_rate = hooks::audio::Resampler::resolve(device_format)) {
        const uint32_t src_rate = device_format->nSamplesPerSec;
        this->resample.setup(device_format, &resample_storage, *target_rate);
        device_format = reinterpret_cast<const WAVEFORMATEX *>(&resample_storage);
        log_info("audio::wasapi", "resample enabled: {} Hz -> {} Hz{}",
                src_rate, *target_rate, this->downmix.enabled ? " (after downmix)" : "");
    }

    // verbose output
    log_info("audio::wasapi", "IAudioClient::Initialize hook hit");
    log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(ShareMode));
    log_info("audio::wasapi", "... StreamFlags       : {}", stream_flags_str(StreamFlags));
    log_info("audio::wasapi", "... hnsBufferDuration : {}", hnsBufferDuration);
    log_info("audio::wasapi", "... hnsPeriodicity    : {}", hnsPeriodicity);
    print_format(device_format);

    if (this->backend) {
        SAFE_CALL("AudioBackend", "on_initialize", this->backend->on_initialize(
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
        this->exclusive_mode = true;
        this->frame_size = device_format->nChannels * (device_format->wBitsPerSample / 8);
    }

    // call next. the resampler owns the device interaction whenever it is active (including when
    // chained after the downmix), otherwise the downmix does, otherwise the device is opened
    // directly.
    HRESULT ret;
    if (this->resample.enabled) {
        ret = this->resample.initialize(
                pReal,
                ShareMode,
                StreamFlags,
                hnsBufferDuration,
                hnsPeriodicity,
                device_format,
                AudioSessionGuid);
    } else if (this->downmix.enabled) {
        ret = this->downmix.initialize(
                pReal,
                ShareMode,
                StreamFlags,
                hnsBufferDuration,
                hnsPeriodicity,
                device_format,
                AudioSessionGuid);
    } else {
        ret = pReal->Initialize(
                ShareMode,
                StreamFlags,
                hnsBufferDuration,
                hnsPeriodicity,
                device_format,
                AudioSessionGuid);
    }

    // check for failure
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IAudioClient", "Initialize", ret);
        return ret;
    }

    log_info("audio::wasapi", "IAudioClient::Initialize success, hr={}", FMT_HRESULT(ret));
    copy_wave_format(&hooks::audio::FORMAT, device_format);
    copy_wave_format(&this->device_format, device_format);

    return ret;
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetBufferSize(UINT32 *pNumBufferFrames) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetBufferSize");
    });

    if (this->backend) {
        uint32_t buffer_frames = 0;

        SAFE_CALL("AudioBackend", "on_get_buffer_size", this->backend->on_get_buffer_size(&buffer_frames));

        if (buffer_frames > 0) {
            *pNumBufferFrames = buffer_frames;

            return S_OK;
        }
    }

    HRESULT ret = pReal->GetBufferSize(pNumBufferFrames);

    // report the buffer size at the game's native rate; the real device buffer is at the
    // resampled rate, so translate it back so the game paces its writes correctly.
    if (SUCCEEDED(ret) && this->resample.enabled && pNumBufferFrames) {
        *pNumBufferFrames = this->resample.frames_device_to_game(*pNumBufferFrames);
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetStreamLatency(REFERENCE_TIME *phnsLatency) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetStreamLatency");
    });

    if (this->backend) {
        REFERENCE_TIME latency = 0;

        SAFE_CALL("AudioBackend", "on_get_stream_latency", this->backend->on_get_stream_latency(
                &latency));

        if (latency > 0) {
            *phnsLatency = latency;

            return S_OK;
        }
    }

    CHECK_RESULT(pReal->GetStreamLatency(phnsLatency));
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetCurrentPadding(UINT32 *pNumPaddingFrames) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetCurrentPadding");
    });

    if (pNumPaddingFrames && this->backend) {
        std::optional<uint32_t> padding_frames;

        SAFE_CALL("AudioBackend", "on_get_current_padding",this->backend->on_get_current_padding(
                padding_frames));

        if (padding_frames.has_value()) {
            *pNumPaddingFrames = padding_frames.value();

            return S_OK;
        }
    }

    HRESULT ret = pReal->GetCurrentPadding(pNumPaddingFrames);

    // the device buffer is at the resampled rate; report padding at the game's native rate so the
    // game's free-space calculation stays paced correctly.
    if (SUCCEEDED(ret) && this->resample.enabled && pNumPaddingFrames) {
        *pNumPaddingFrames = this->resample.padding_device_to_game(*pNumPaddingFrames);
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::IsFormatSupported(
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

    // when downmixing, the real device is opened as stereo, so check whether the equivalent
    // stereo format is supported instead of the multi-channel one. when resampling is also active
    // it chains onto that stereo format, so check the resampled stereo format.
    if (resolve_downmix(pFormat)) {
        WAVEFORMATEXTENSIBLE stereo_storage = {};
        hooks::audio::Downmix::make_stereo_format(pFormat, &stereo_storage);
        const WAVEFORMATEX *check_format = reinterpret_cast<const WAVEFORMATEX *>(&stereo_storage);

        WAVEFORMATEXTENSIBLE resample_storage = {};
        if (auto target_rate = hooks::audio::Resampler::resolve(check_format)) {
            hooks::audio::Resampler::make_device_format(check_format, &resample_storage, *target_rate);
            check_format = reinterpret_cast<const WAVEFORMATEX *>(&resample_storage);
        }

        CHECK_RESULT(pReal->IsFormatSupported(ShareMode, check_format, ppClosestMatch));
    } else if (games::gitadora::is_arena_model()) {
        games::gitadora::fix_audio_channel_mask(const_cast<WAVEFORMATEX *>(pFormat));
    } else if (auto target_rate = hooks::audio::Resampler::resolve(pFormat)) {

        // when resampling, the real device is opened at the target rate, so check whether the
        // equivalent format at that rate is supported instead of the game's native rate.
        WAVEFORMATEXTENSIBLE resample_storage = {};
        hooks::audio::Resampler::make_device_format(pFormat, &resample_storage, *target_rate);
        const auto resample_format = reinterpret_cast<const WAVEFORMATEX *>(&resample_storage);

        CHECK_RESULT(pReal->IsFormatSupported(ShareMode, resample_format, ppClosestMatch));
    }

    if (this->backend) {
        HRESULT ret = this->backend->on_is_format_supported(&ShareMode, pFormat, ppClosestMatch);

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
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetMixFormat(WAVEFORMATEX **ppDeviceFormat) {
    WRAP_VERBOSE;

    if (!ppDeviceFormat) {
        return E_POINTER;
    }

    if (this->backend) {
        HRESULT ret = this->backend->on_get_mix_format(ppDeviceFormat);

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
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetDevicePeriod(
    REFERENCE_TIME *phnsDefaultDevicePeriod,
    REFERENCE_TIME *phnsMinimumDevicePeriod)
{
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioClient::GetDevicePeriod");
    });

    HRESULT ret = pReal->GetDevicePeriod(phnsDefaultDevicePeriod, phnsMinimumDevicePeriod);

    if (SUCCEEDED(ret) && this->backend) {
        SAFE_CALL("AudioBackend", "on_get_device_period", this->backend->on_get_device_period(
                phnsDefaultDevicePeriod,
                phnsMinimumDevicePeriod));
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Start() {
    WRAP_VERBOSE;

    HRESULT ret = pReal->Start();

    if (SUCCEEDED(ret) && this->backend) {
        SAFE_CALL("AudioBackend", "on_start", this->backend->on_start());
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Stop() {
    WRAP_VERBOSE;

    HRESULT ret = pReal->Stop();

    if (SUCCEEDED(ret) && this->backend) {
        SAFE_CALL("AudioBackend", "on_stop", this->backend->on_stop());
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::Reset() {
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->Reset());
}
HRESULT STDMETHODCALLTYPE WrappedIAudioClient::SetEventHandle(HANDLE eventHandle) {
    WRAP_VERBOSE;

    if (this->backend) {
        SAFE_CALL("AudioBackend", "on_set_event_handle", this->backend->on_set_event_handle(&eventHandle));
    }

    CHECK_RESULT(pReal->SetEventHandle(eventHandle));
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

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::IsOffloadCapable(
        AUDIO_STREAM_CATEGORY Category,
        BOOL *pbOffloadCapable) {
    WRAP_VERBOSE;

    CHECK_RESULT(pReal3->IsOffloadCapable(Category, pbOffloadCapable));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::SetClientProperties( 
    const AudioClientProperties *pProperties) {

    WRAP_VERBOSE;
    CHECK_RESULT(pReal3->SetClientProperties(pProperties));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetBufferSizeLimits( 
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

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetSharedModeEnginePeriod( 
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

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::GetCurrentSharedModeEnginePeriod( 
    WAVEFORMATEX **ppFormat,
    UINT32 *pCurrentPeriodInFrames) {

    WRAP_VERBOSE;
    CHECK_RESULT(pReal3->GetCurrentSharedModeEnginePeriod(
        ppFormat,
        pCurrentPeriodInFrames));
}

HRESULT STDMETHODCALLTYPE WrappedIAudioClient::InitializeSharedAudioStream( 
    DWORD StreamFlags,
    UINT32 PeriodInFrames,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) {

    if (!pFormat) {
        return E_POINTER;
    }

    // verbose output
    log_info("audio::wasapi", "IAudioClient3::InitializeSharedAudioStream hook hit");
    log_info("audio::wasapi", "... ShareMode         : {}", share_mode_str(AUDCLNT_SHAREMODE_SHARED));
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
    copy_wave_format(&this->device_format, pFormat);
    return ret;
}
