#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "external/asio/asio.h"
#include "external/asio/iasiodrv.h"
#include "external/readerwriterqueue/readerwriterqueue.h"
#include "hooks/audio/audio_private.h"
#include "hooks/audio/buffer.h"

#include "backend.h"

struct AsioBackend;

extern AsioBackend *ASIO_BACKEND;

struct BufferEntry {
    BYTE *buffer;
    size_t length;
    size_t read;
};

struct AsioInstanceInfo {
    long inputs = 0;
    long outputs = 0;
    long buffer_min_size = 0;
    long buffer_max_size = 0;
    long buffer_preferred_size = 0;
    long buffer_granularity = 0;
    long input_latency = 0;
    long output_latency = 0;
};

struct AsioBackend final : AudioBackend {
public:
    explicit AsioBackend();

    ~AsioBackend() final;

    const WAVEFORMATEXTENSIBLE &format() const noexcept override;

    HRESULT on_initialize(
        AUDCLNT_SHAREMODE *ShareMode,
        DWORD *StreamFlags,
        REFERENCE_TIME *hnsBufferDuration,
        REFERENCE_TIME *hnsPeriodicity,
        const WAVEFORMATEX *pFormat,
        LPCGUID AudioSessionGuid) noexcept override;

    HRESULT on_get_buffer_size(uint32_t *buffer_frames) noexcept override;
    HRESULT on_get_stream_latency(REFERENCE_TIME *latency) noexcept override;
    HRESULT on_get_current_padding(std::optional<uint32_t> &padding_frames) noexcept override;

    HRESULT on_is_format_supported(
        AUDCLNT_SHAREMODE *ShareMode,
        const WAVEFORMATEX *pFormat,
        WAVEFORMATEX **ppClosestMatch) noexcept override;

    HRESULT on_get_mix_format(WAVEFORMATEX **pp_device_format) noexcept override;

    HRESULT on_get_device_period(
        REFERENCE_TIME *default_device_period,
        REFERENCE_TIME *minimum_device_period) noexcept override;

    HRESULT on_start() noexcept override;
    HRESULT on_stop() noexcept override;
    HRESULT on_set_event_handle(HANDLE *event_handle) noexcept override;

    HRESULT on_get_buffer(uint32_t num_frames_requested, BYTE **pp_data) noexcept override;
    HRESULT on_release_buffer(uint32_t num_frames_written, DWORD dwFlags) noexcept override;

    // for overlay
    inline const AsioDriverInfo &driver_info() const noexcept {
        return this->driver_info_;
    }
    inline const std::vector<AsioChannelInfo> &channel_info() const noexcept {
        return this->asio_channel_info_;
    }
    inline const AsioInstanceInfo &asio_info() const noexcept {
        return this->asio_info_;
    }
    void open_control_panel();

    std::atomic<uint32_t> queued_frames = 0;
    std::atomic<size_t> queued_bytes = 0;

private:
    using AsioFunction = std::function<AsioError()>;

    enum class AsioThreadState {
        Closed,
        Failed,
        Running,
        ShuttingDown,
    };
    struct AsioThreadMessage {
        AsioFunction fn;
        bool result_needed;
    };

    void set_thread_state(AsioThreadState state);
    bool load_driver();
    bool update_driver_info();
    bool update_latency();
    bool set_initial_format(WAVEFORMATEXTENSIBLE &target);
    bool init();
    bool unload_driver();
    void reset();
    AsioError run_on_asio_thread(AsioFunction fn, bool result_needed = true);

    // ASIO callbacks
    static void buffer_switch(long double_buffer_index, AsioBool direct_process);
    static void sample_rate_did_change(AsioSampleRate sample_rate);
    static long asio_message(long selector, long value, void *message, double *opt);

    // helper methods
    static bool is_supported_subformat(const WAVEFORMATEXTENSIBLE &format_ex) noexcept;
    REFERENCE_TIME compute_ref_time() const;
    REFERENCE_TIME compute_latency_ref_time() const;

    std::thread asio_thread;
    std::atomic_bool asio_thread_initialized = false;
    std::mutex asio_thread_state_lock;
    // TODO: use `std::atomic<T>::wait` when stabilized in MSVC
    std::condition_variable asio_thread_state_cv;
    std::atomic<AsioThreadState> asio_thread_state = AsioThreadState::Closed;
    moodycamel::BlockingReaderWriterQueue<AsioThreadMessage> asio_msg_queue_func;
    moodycamel::BlockingReaderWriterQueue<AsioError> asio_msg_queue_result;

    moodycamel::ReaderWriterQueue<BufferEntry> queue;
    std::optional<HANDLE> relay_handle = std::nullopt;

    IAsio *asio_driver = nullptr;
    AsioCallbacks asio_callbacks {};
    AsioDriverInfo driver_info_ {};
    AsioInstanceInfo asio_info_;
    std::vector<AsioChannelInfo> asio_channel_info_;
    std::vector<AsioBufferInfo> asio_buffers;
    SampleType asio_sample_type = SampleType::UNSUPPORTED;

    std::atomic_bool started = false;
    WAVEFORMATEXTENSIBLE format_ {};
    WAVEFORMATEXTENSIBLE last_checked_format {};

    //std::vector<BYTE> last_sound_buffer;
    std::vector<double> conversion_sound_buffer;
    BYTE *active_sound_buffer = nullptr;
};
