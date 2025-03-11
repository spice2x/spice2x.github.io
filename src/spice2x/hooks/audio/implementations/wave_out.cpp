#include "wave_out.h"

#include "hooks/audio/audio.h"
#include "hooks/audio/backends/wasapi/audio_client.h"
#include "hooks/audio/backends/wasapi/defs.h"

static REFERENCE_TIME WASAPI_TARGET_REFTIME = TARGET_REFTIME;

HRESULT WaveOutBackend::init(uint32_t buffer_size) {
    auto &format = hooks::audio::FORMAT.Format;
    format.wFormatTag = WAVE_FORMAT_PCM;

    log_info("audio::wave_out", "initializing waveOut backend with {} channels, {} Hz, {}-bit",
             format.nChannels,
             format.nSamplesPerSec,
             format.wBitsPerSample);
    log_info("audio::wave_out", "... nBlockAlign     : {} bytes", format.nBlockAlign);
    log_info("audio::wave_out", "... nAvgBytesPerSec : {} bytes", format.nAvgBytesPerSec);
    log_info("audio::wave_out", "... buffer reftime  : {} ms", WASAPI_TARGET_REFTIME / 10000.f);
    log_info("audio::wave_out", "... buffer count    : {} buffers", _countof(this->hdrs));

    MMRESULT ret = waveOutOpen(
            &this->handle,
            WAVE_MAPPER,
            reinterpret_cast<const WAVEFORMATEX *>(&hooks::audio::FORMAT.Format),
            reinterpret_cast<DWORD_PTR>(this->dispatcher_event),
            reinterpret_cast<DWORD_PTR>(nullptr),
            CALLBACK_EVENT);

    if (ret != MMSYSERR_NOERROR) {
        log_warning("audio::wave_out", "failed to initialize waveOut backend, hr={:#08x}",
                    static_cast<unsigned>(ret));

        return static_cast<HRESULT>(ret);
    }

    // initialize buffers
    for (auto &hdr : this->hdrs) {
        memset(&hdr, 0, sizeof(hdr));
        hdr.lpData = new char[buffer_size] {};
        hdr.dwBufferLength = buffer_size;
        hdr.dwBytesRecorded = 0;
        hdr.dwUser = 0;
        hdr.dwFlags = 0;
        hdr.dwLoops = 0;
        hdr.lpNext = nullptr;
        ret = waveOutPrepareHeader(this->handle, &hdr, sizeof(hdr));

        if (ret != MMSYSERR_NOERROR) {
            log_warning("audio::wave_out", "failed to prepare waveOut header, hr=0x{:08x}",
                        static_cast<unsigned>(ret));

            return static_cast<HRESULT>(ret);
        }

        ret = waveOutWrite(this->handle, &hdr, sizeof(hdr));

        if (ret != MMSYSERR_NOERROR) {
            log_warning("audio::wave_out", "failed to write waveOut header, hr=0x{:08x}",
                    static_cast<unsigned>(ret));

            return static_cast<HRESULT>(ret);
        }
    }

    // mark as initialized
    this->initialized = true;

    return S_OK;
}

const WAVEFORMATEXTENSIBLE &WaveOutBackend::format() const noexcept {
    return hooks::audio::FORMAT;
}

HRESULT WaveOutBackend::on_initialize(
    AUDCLNT_SHAREMODE *ShareMode,
    DWORD *StreamFlags,
    REFERENCE_TIME *hnsBufferDuration,
    REFERENCE_TIME *hnsPeriodicity,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) noexcept
{
    *ShareMode = AUDCLNT_SHAREMODE_SHARED;
    *StreamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_RATEADJUST |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    *hnsBufferDuration = WASAPI_TARGET_REFTIME;
    *hnsPeriodicity = WASAPI_TARGET_REFTIME;

    // this backend only supports stereo audio
    if (pFormat->nChannels > 2) {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    return S_OK;
}
HRESULT WaveOutBackend::on_get_buffer_size(uint32_t *buffer_frames) noexcept {
    *buffer_frames = _countof(this->hdrs);

    return S_OK;
}
HRESULT WaveOutBackend::on_get_stream_latency(REFERENCE_TIME *latency) noexcept {
    *latency = WASAPI_TARGET_REFTIME;

    return S_OK;
}
HRESULT WaveOutBackend::on_get_current_padding(std::optional<uint32_t> &padding_frames) noexcept {
    size_t queued_bytes = 0;

    for (auto &hdr : this->hdrs) {
        if (hdr.dwFlags & WHDR_DONE) {
            queued_bytes += static_cast<unsigned>(hdr.dwBufferLength);
        }
    }

    auto frames = static_cast<uint32_t>(queued_bytes / hooks::audio::FORMAT.Format.nBlockAlign);
    //log_info("audio::wave_out", "queued_bytes = {}, frames = {}", queued_bytes, frames);

    padding_frames = frames;

    return S_OK;
}
HRESULT WaveOutBackend::on_is_format_supported(
    AUDCLNT_SHAREMODE *ShareMode,
    const WAVEFORMATEX *pFormat,
    WAVEFORMATEX **ppClosestMatch) noexcept
{
    // always support 44.1 kHz, stereo, 16-bits per channel with custom backends
    if (*ShareMode == AUDCLNT_SHAREMODE_EXCLUSIVE &&
        pFormat->nChannels == 2 &&
        pFormat->nSamplesPerSec == 44100 &&
        pFormat->wBitsPerSample == 16)
    {
        return S_OK;
    }

    return AUDCLNT_E_UNSUPPORTED_FORMAT;
}
HRESULT WaveOutBackend::on_get_mix_format(WAVEFORMATEX **pp_device_format) noexcept {
    return E_NOTIMPL;
}
HRESULT WaveOutBackend::on_get_device_period(
    REFERENCE_TIME *default_device_period,
    REFERENCE_TIME *minimum_device_period)
{
    *default_device_period = WASAPI_TARGET_REFTIME;
    *minimum_device_period = WASAPI_TARGET_REFTIME;

    return S_OK;
}
HRESULT WaveOutBackend::on_start() noexcept {
    return S_OK;
}
HRESULT WaveOutBackend::on_stop() noexcept {
    return S_OK;
}
HRESULT WaveOutBackend::on_set_event_handle(HANDLE *event_handle) {
    this->relay_event = *event_handle;
    this->dispatcher_event = CreateEvent(nullptr, true, false, nullptr);

    *event_handle = this->dispatcher_event;

    return S_OK;
}

HRESULT WaveOutBackend::on_get_buffer(uint32_t num_frames_requested, BYTE **ppData) {
    auto buffer_size = hooks::audio::FORMAT.Format.nBlockAlign * num_frames_requested;

    if (!this->initialized) {
        this->init(buffer_size);
    }

    // wait for a free slot
    WaitForSingleObject(this->dispatcher_event, INFINITE);

    // allocate temporary sound buffer
    this->active_sound_buffer = reinterpret_cast<BYTE *>(CoTaskMemAlloc(buffer_size));

    // hand the buffer to the callee
    *ppData = this->active_sound_buffer;

    return S_OK;
}
HRESULT WaveOutBackend::on_release_buffer(uint32_t num_frames_written, DWORD dwFlags) {
    bool written = false;

    // reset the dispatcher event
    ResetEvent(this->dispatcher_event);

    while (!written) {
        for (WAVEHDR &hdr : this->hdrs) {
            if (hdr.dwFlags & WHDR_DONE) {
                memcpy(hdr.lpData, this->active_sound_buffer, hdr.dwBufferLength);

                // write the data to the device now
                MMRESULT ret = waveOutWrite(this->handle, &hdr, sizeof(hdr));

                if (ret != MMSYSERR_NOERROR) {
                    log_warning("audio::wave_out", "failed to write waveOut data, hr={:#08x}",
                            static_cast<unsigned>(ret));
                }

                written = true;
                break;
            }
        }

        // avoid pegging the CPU
        if (!written) {
            Sleep(1);
        }
    }

    // free temporary sound buffer
    CoTaskMemFree(this->active_sound_buffer);
    this->active_sound_buffer = nullptr;

    // trigger game audio callback
    SetEvent(this->relay_event);

    return S_OK;
}
