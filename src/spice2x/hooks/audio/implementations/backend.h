#pragma once

#include <cstdint>
#include <optional>

#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>

struct WrappedIAudioClient;

struct AudioBackend {
public:
    virtual ~AudioBackend() = default;

    [[nodiscard]] virtual const WAVEFORMATEXTENSIBLE &format() const noexcept = 0;

#pragma region IAudioClient
    virtual HRESULT on_initialize(
        AUDCLNT_SHAREMODE *ShareMode,
        DWORD *StreamFlags,
        REFERENCE_TIME *hnsBufferDuration,
        REFERENCE_TIME *hnsPeriodicity,
        const WAVEFORMATEX *pFormat,
        LPCGUID AudioSessionGuid) = 0;

    virtual HRESULT on_get_buffer_size(uint32_t *buffer_frames) = 0;

    virtual HRESULT on_get_stream_latency(REFERENCE_TIME *latency) = 0;

    virtual HRESULT on_get_current_padding(std::optional<uint32_t> &padding_frames) = 0;

    virtual HRESULT on_is_format_supported(
        AUDCLNT_SHAREMODE *ShareMode,
        const WAVEFORMATEX *pFormat,
        WAVEFORMATEX **ppClosestMatch) = 0;

    virtual HRESULT on_get_mix_format(WAVEFORMATEX **pp_device_format) = 0;

    virtual HRESULT on_get_device_period(
        REFERENCE_TIME *default_device_period,
        REFERENCE_TIME *minimum_device_period) = 0;

    virtual HRESULT on_start() = 0;

    virtual HRESULT on_stop() = 0;

    virtual HRESULT on_set_event_handle(HANDLE *event_handle) = 0;
#pragma endregion

#pragma region IAudioRenderClient
    virtual HRESULT on_get_buffer(uint32_t num_frames_requested, BYTE **ppData) = 0;
    virtual HRESULT on_release_buffer(uint32_t num_frames_written, DWORD dwFlags) = 0;
#pragma endregion
};
