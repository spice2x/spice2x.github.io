#pragma once

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#include <audioclient.h>

#include "hooks/audio/implementations/backend.h"

// discards all audio while pacing the game's event handle once per buffer period, so the game
// keeps running normally with nothing output to any real device. routed through the shared
// DummyIAudioClient, the same plumbing the asio backend uses.
struct NullDiscardBackend final : AudioBackend {
    ~NullDiscardBackend() final;

    const WAVEFORMATEXTENSIBLE &format() const noexcept override;

    HRESULT on_initialize(
        AUDCLNT_SHAREMODE *,
        DWORD *,
        REFERENCE_TIME *hnsBufferDuration,
        REFERENCE_TIME *,
        const WAVEFORMATEX *pFormat,
        LPCGUID) override;
    HRESULT on_get_buffer_size(uint32_t *buffer_frames) override;
    HRESULT on_get_stream_latency(REFERENCE_TIME *latency) override;
    HRESULT on_get_current_padding(std::optional<uint32_t> &padding_frames) override;
    HRESULT on_is_format_supported(
        AUDCLNT_SHAREMODE *,
        const WAVEFORMATEX *,
        WAVEFORMATEX **ppClosestMatch) override;
    HRESULT on_get_mix_format(WAVEFORMATEX **) override;
    HRESULT on_get_device_period(
        REFERENCE_TIME *default_device_period,
        REFERENCE_TIME *minimum_device_period) override;
    HRESULT on_start() override;
    HRESULT on_stop() override;
    HRESULT on_set_event_handle(HANDLE *event_handle) override;
    HRESULT on_get_buffer(uint32_t num_frames_requested, BYTE **ppData) override;
    HRESULT on_release_buffer(uint32_t, DWORD) override;

private:
    void pace_loop();

    WAVEFORMATEXTENSIBLE format_ {};
    uint32_t buffer_frames = 0;
    REFERENCE_TIME period_reftime = 0;
    HANDLE relay_handle = nullptr;
    std::vector<BYTE> scratch;
    std::thread pacing_thread;
    std::atomic<bool> running = false;
};
