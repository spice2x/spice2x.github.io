#include "null_discard_backend.h"

#include <algorithm>
#include <chrono>

#include "hooks/audio/util.h"
#include "util/logging.h"
#include "util/precise_timer.h"

NullDiscardBackend::~NullDiscardBackend() {
    this->running = false;
    if (this->pacing_thread.joinable()) {
        this->pacing_thread.join();
    }
}

const WAVEFORMATEXTENSIBLE &NullDiscardBackend::format() const noexcept {
    return this->format_;
}

HRESULT NullDiscardBackend::on_initialize(
    AUDCLNT_SHAREMODE *,
    DWORD *,
    REFERENCE_TIME *hnsBufferDuration,
    REFERENCE_TIME *,
    const WAVEFORMATEX *pFormat,
    LPCGUID)
{
    copy_wave_format(&this->format_, pFormat);

    // honor the game's requested buffer duration, falling back to 10 ms
    constexpr REFERENCE_TIME DEFAULT_REFTIME = 100000; // 10 ms in 100-ns units
    this->period_reftime = (hnsBufferDuration && *hnsBufferDuration > 0)
            ? *hnsBufferDuration
            : DEFAULT_REFTIME;

    this->buffer_frames = std::max<uint32_t>(1, static_cast<uint32_t>(
            static_cast<double>(this->format_.Format.nSamplesPerSec)
                    * this->period_reftime / 10000000.0 + 0.5));

    log_info("audio::null", "initializing null render device with {} channels, {} Hz, {}-bit",
             this->format_.Format.nChannels,
             this->format_.Format.nSamplesPerSec,
             this->format_.Format.wBitsPerSample);

    return S_OK;
}

HRESULT NullDiscardBackend::on_get_buffer_size(uint32_t *buffer_frames) {
    *buffer_frames = this->buffer_frames;
    return S_OK;
}

HRESULT NullDiscardBackend::on_get_stream_latency(REFERENCE_TIME *latency) {
    *latency = this->period_reftime;
    return S_OK;
}

HRESULT NullDiscardBackend::on_get_current_padding(std::optional<uint32_t> &padding_frames) {
    // discarded immediately, so the buffer always reads as fully drained
    padding_frames = 0;
    return S_OK;
}

HRESULT NullDiscardBackend::on_is_format_supported(
    AUDCLNT_SHAREMODE *,
    const WAVEFORMATEX *,
    WAVEFORMATEX **ppClosestMatch)
{
    if (ppClosestMatch) {
        *ppClosestMatch = nullptr;
    }
    return S_OK;
}

HRESULT NullDiscardBackend::on_get_mix_format(WAVEFORMATEX **) {
    return E_NOTIMPL;
}

HRESULT NullDiscardBackend::on_get_device_period(
    REFERENCE_TIME *default_device_period,
    REFERENCE_TIME *minimum_device_period)
{
    if (default_device_period) {
        *default_device_period = this->period_reftime;
    }
    if (minimum_device_period) {
        *minimum_device_period = this->period_reftime;
    }
    return S_OK;
}

HRESULT NullDiscardBackend::on_start() {
    if (!this->running.exchange(true)) {
        this->pacing_thread = std::thread(&NullDiscardBackend::pace_loop, this);
    }
    return S_OK;
}

HRESULT NullDiscardBackend::on_stop() {
    return S_OK;
}

HRESULT NullDiscardBackend::on_set_event_handle(HANDLE *event_handle) {
    // keep the game's event so pace_loop() can wake it; there is no real device behind it
    this->relay_handle = *event_handle;
    return S_OK;
}

HRESULT NullDiscardBackend::on_get_buffer(uint32_t num_frames_requested, BYTE **ppData) {
    const size_t buffer_size =
            static_cast<size_t>(this->format_.Format.nBlockAlign) * num_frames_requested;
    if (this->scratch.size() < buffer_size) {
        this->scratch.resize(buffer_size);
    }
    *ppData = this->scratch.data();
    return S_OK;
}

HRESULT NullDiscardBackend::on_release_buffer(uint32_t, DWORD) {
    // discard the audio entirely
    return S_OK;
}

void NullDiscardBackend::pace_loop() {
    using namespace std::chrono;

    timeutils::PreciseSleepTimer timer;

    // audio is discarded, so timing precision and drift do not matter; just wake the
    // game once per buffer period to keep its render thread from blocking on the event.
    const auto period = duration_cast<steady_clock::duration>(
            duration<double>(this->period_reftime / 10000000.0));

    while (this->running.load()) {
        if (this->relay_handle) {
            SetEvent(this->relay_handle);
        }
        timer.sleep(period);
    }
}
