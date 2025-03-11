#include "asio.h"

#include <mutex>
#include <system_error>
#include <utility>

#include <audioclient.h>

#include "external/asio/asiolist.h"
#include "hooks/audio/audio.h"
#include "hooks/audio/audio_private.h"
#include "hooks/audio/util.h"
#include "hooks/audio/backends/wasapi/defs.h"
#include "util/flags_helper.h"
#include "util/logging.h"

// std::max
#ifdef max
#undef max
#endif

// std::min
#ifdef min
#undef min
#endif

constexpr double REFTIMES_PER_SEC = 10000000.;

AsioBackend *ASIO_BACKEND = nullptr;

static std::string asio_error_str(AsioError error) {
    switch (error) {
        ENUM_VARIANT(ASE_OK);
        ENUM_VARIANT(ASE_Success);
        ENUM_VARIANT(ASE_NotPresent);
        ENUM_VARIANT(ASE_HWMalfunction);
        ENUM_VARIANT(ASE_InvalidParameter);
        ENUM_VARIANT(ASE_InvalidMode);
        ENUM_VARIANT(ASE_SPNotAdvancing);
        ENUM_VARIANT(ASE_NoClock);
        ENUM_VARIANT(ASE_NoMemory);
        default:
            return fmt::to_string(static_cast<unsigned>(error));
    }
}

static std::string asio_sample_type_str(AsioSampleType type) {
    switch (type) {
        ENUM_VARIANT(ASIOSTInt16MSB);
        ENUM_VARIANT(ASIOSTInt24MSB);
        ENUM_VARIANT(ASIOSTInt32MSB);
        ENUM_VARIANT(ASIOSTFloat32MSB);
        ENUM_VARIANT(ASIOSTFloat64MSB);
        ENUM_VARIANT(ASIOSTInt32MSB16);
        ENUM_VARIANT(ASIOSTInt32MSB18);
        ENUM_VARIANT(ASIOSTInt32MSB20);
        ENUM_VARIANT(ASIOSTInt32MSB24);
        ENUM_VARIANT(ASIOSTInt16LSB);
        ENUM_VARIANT(ASIOSTInt24LSB);
        ENUM_VARIANT(ASIOSTInt32LSB);
        ENUM_VARIANT(ASIOSTFloat32LSB);
        ENUM_VARIANT(ASIOSTFloat64LSB);
        ENUM_VARIANT(ASIOSTInt32LSB16);
        ENUM_VARIANT(ASIOSTInt32LSB18);
        ENUM_VARIANT(ASIOSTInt32LSB20);
        ENUM_VARIANT(ASIOSTInt32LSB24);
        ENUM_VARIANT(ASIOSTDSDInt8LSB1);
        ENUM_VARIANT(ASIOSTDSDInt8MSB1);
        ENUM_VARIANT(ASIOSTDSDInt8NER8);
        default:
            return fmt::to_string(type);
    }
}

static SampleType convert_windows_format(const WAVEFORMATEXTENSIBLE &format_ex) {
    const auto &format = format_ex.Format;

    bool pcm_format = false;
    bool float_format = false;

    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        pcm_format = true;
    } else if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        float_format = true;
    } else if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (format_ex.SubFormat == GUID_KSDATAFORMAT_SUBTYPE_PCM) {
            pcm_format = true;
        } else if (format_ex.SubFormat == GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            float_format = true;
        }
    }

    if (pcm_format) {
        switch (format.wBitsPerSample) {
            case 16:
                return SampleType::SINT_16;
            case 24:
                return SampleType::SINT_24;
            case 32:
                return SampleType::SINT_32;
            default:
                return SampleType::UNSUPPORTED;
        }
    } else if (float_format) {
        switch (format.wBitsPerSample) {
            case 32:
                return SampleType::FLOAT_32;
            case 64:
                return SampleType::FLOAT_64;
            default:
                return SampleType::UNSUPPORTED;
        }
    } else {
        return SampleType::UNSUPPORTED;
    }
}

static SampleType convert_asio_sample_type(AsioSampleType type) {
    switch (type) {
        case ASIOSTInt16LSB:
            return SampleType::SINT_16;
        case ASIOSTInt24LSB:
            return SampleType::SINT_24;
        case ASIOSTInt32LSB:
            return SampleType::SINT_32;
        case ASIOSTFloat32LSB:
            return SampleType::FLOAT_32;
        case ASIOSTFloat64LSB:
            return SampleType::FLOAT_64;
        default:
            return SampleType::UNSUPPORTED;
    }
}

AsioBackend::AsioBackend() {
    this->asio_thread = std::thread([this]() {
        std::unique_lock<std::mutex> lock_handle(this->asio_thread_state_lock);

        log_info("audio::asio", "initializing ASIO thread");

        if (this->load_driver() &&
            this->update_driver_info() &&
            this->set_initial_format(this->last_checked_format))
        {
            this->asio_thread_state = AsioThreadState::Running;
        } else {
            this->asio_thread_state = AsioThreadState::Failed;
        }

        // Notify condition variable waiters of thread state update
        lock_handle.unlock();
        this->asio_thread_state_cv.notify_all();

        if (this->asio_thread_state.load() != AsioThreadState::Running) {
            return;
        }

        log_info("audio::asio", "ASIO thread entering main loop");

        this->asio_thread_initialized = true;

        while (this->asio_thread_state.load() == AsioThreadState::Running) {
            struct AsioThreadMessage msg;
            this->asio_msg_queue_func.wait_dequeue(msg);

            auto result = msg.fn();

            if (msg.result_needed) {
                this->asio_msg_queue_result.enqueue(result);
            }
        }

        this->asio_thread_initialized = false;
    });

    std::unique_lock<std::mutex> lock_handle(this->asio_thread_state_lock);
    this->asio_thread_state_cv.wait(lock_handle, [this]() {
        return this->asio_thread_state.load() != AsioThreadState::Closed;
    });

    if (this->asio_thread_state.load() == AsioThreadState::Failed) {
        log_fatal("audio::asio", "failed to initialize ASIO thread");
    }
}
AsioBackend::~AsioBackend() {
    log_info("audio::asio", "shutting down ASIO backend");

    if (this->asio_thread_initialized) {
        this->run_on_asio_thread([this]() {
            this->unload_driver();

            return ASE_OK;
        });
    }

    // shut down ASIO handler thread
    this->set_thread_state(AsioThreadState::ShuttingDown);

    // enqueue function to break event loop
    if (this->asio_thread_initialized) {
        this->run_on_asio_thread([]() {
            return ASE_OK;
        });
    }

    this->asio_thread.join();
}

void AsioBackend::set_thread_state(AsioThreadState state) {
    std::unique_lock<std::mutex> lock_handle(this->asio_thread_state_lock);

    this->asio_thread_state = state;

    // Notify condition variable waiters of thread state update
    lock_handle.unlock();
    this->asio_thread_state_cv.notify_all();
}
bool AsioBackend::load_driver() {
    AsioDriverList asio_driver_list;

    for (const auto &driver : asio_driver_list.driver_list) {
        log_info("audio::asio", "Driver {}", driver.id);
        log_info("audio::asio", "... Name : {}", driver.name);
        log_info("audio::asio", "... Path : {}", driver.dll_path);
    }

    const auto driver_id = hooks::audio::ASIO_DRIVER_ID;

    IAsio *driver = nullptr;
    auto ret = asio_driver_list.open_driver(driver_id, reinterpret_cast<void **>(&driver));
    if (ret != 0) {
        log_warning("audio::asio", "failed to open driver: {}", FMT_HRESULT(ret));
    } else if (driver) {
        this->driver_info_.sys_ref = GetDesktopWindow();
        if (!driver->init(this->driver_info_.sys_ref)) {
            driver->get_error_message(this->driver_info_.error_message);
            driver = nullptr;

            log_warning("audio::asio", "failed to initialize driver: {}", this->driver_info_.error_message);
        } else {
            this->asio_driver = driver;
            this->asio_driver->AddRef();
        }
    }

    return this->asio_driver != nullptr;
}
bool AsioBackend::update_driver_info() {
    memset(&this->driver_info_.name, 0, sizeof(this->driver_info_.name));
    this->driver_info_.asio_version = 0;
    this->driver_info_.driver_version = 0;

    memset(&this->asio_info_, 0, sizeof(this->asio_info_));

    if (!this->asio_driver) {
        log_warning("audio::asio", "attempted to update driver info when no driver is loaded");
        return false;
    }

    this->driver_info_.asio_version = 2;

    // `get_driver_name` and `get_driver_version` are not supposed to fail according to the
    // function definitions and the documentation
    this->asio_driver->get_driver_name(this->driver_info_.name);
    this->driver_info_.driver_version = this->asio_driver->get_driver_version();

    // the rest of the functions can fail however
    AsioError result;

    result = this->asio_driver->get_channels(&this->asio_info_.inputs, &this->asio_info_.outputs);
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to get channels: {}", asio_error_str(result));
        return false;
    }

    result = this->asio_driver->get_buffer_size(
            &this->asio_info_.buffer_min_size,
            &this->asio_info_.buffer_max_size,
            &this->asio_info_.buffer_preferred_size,
            &this->asio_info_.buffer_granularity);
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to get buffer sizes: {}", asio_error_str(result));
        return false;
    }

    for (long i = 0; i < this->asio_info_.outputs; i++) {
        auto &channel_info = this->asio_channel_info_.emplace_back();
        channel_info.channel = i;
        channel_info.is_input = AsioFalse;

        result = this->asio_driver->get_channel_info(&channel_info);
        if (result != ASE_OK) {
            log_warning("audio::asio", "failed to get channel {} info: {}", i, asio_error_str(result));
            return false;
        }
    }

    log_info("audio::asio", "Device Info:");
    log_info("audio::asio", "... Name               : {}", this->driver_info_.name);
    log_info("audio::asio", "... Version            : {}", this->driver_info_.driver_version);
    log_info("audio::asio", "... Inputs             : {} channels", this->asio_info_.inputs);
    log_info("audio::asio", "... Outputs            : {} channels", this->asio_info_.outputs);
    log_info("audio::asio", "... Buffer Minimum     : {} samples", this->asio_info_.buffer_min_size);
    log_info("audio::asio", "... Buffer Maximum     : {} samples", this->asio_info_.buffer_max_size);
    log_info("audio::asio", "... Buffer Preferred   : {} samples", this->asio_info_.buffer_preferred_size);
    log_info("audio::asio", "... Buffer Granularity : {} samples", this->asio_info_.buffer_granularity);

    log_info("audio::asio", "Channel Info:");
    for (const auto &channel_info : this->asio_channel_info_) {
        log_info("audio::asio", "... Channel {}: {} (group: {}, type: {})",
                channel_info.channel,
                channel_info.name,
                channel_info.channel_group,
                asio_sample_type_str(channel_info.type));
    }

    return true;
}
bool AsioBackend::update_latency() {
    auto result = this->asio_driver->get_latencies(
        &this->asio_info_.input_latency,
        &this->asio_info_.output_latency);
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to get latency: {}", asio_error_str(result));
        return false;
    }

    return true;
}
bool AsioBackend::set_initial_format(WAVEFORMATEXTENSIBLE &target) {
    AsioSampleRate sample_rate = 0.0;

    // get current sample rate
    auto result = this->asio_driver->get_sample_rate(&sample_rate);
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to get current sample rate: {}", asio_error_str(result));
        return false;
    }

    // set initial format state
    target.Format.wFormatTag = WAVE_FORMAT_PCM;
    target.Format.nChannels = static_cast<WORD>(this->asio_info_.outputs);
    target.Format.nSamplesPerSec = static_cast<WORD>(sample_rate);
    target.Format.nBlockAlign = target.Format.nChannels * (target.Format.wBitsPerSample / 8);
    target.Format.nAvgBytesPerSec = target.Format.nSamplesPerSec * target.Format.nBlockAlign;
    target.Format.cbSize = 0;

    // set initial sub-format
    if (!this->asio_channel_info_.empty()) {
        this->asio_sample_type = convert_asio_sample_type(this->asio_channel_info_[0].type);

        // mark this as the extensible struct
        target.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        target.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        target.dwChannelMask = SPEAKER_ALL;

        switch (this->asio_sample_type) {
            case SampleType::SINT_16:
                target.Format.wBitsPerSample = 16;
                target.Samples.wValidBitsPerSample = 16;
                target.SubFormat = GUID_KSDATAFORMAT_SUBTYPE_PCM;
                break;
            case SampleType::SINT_24:
                target.Format.wBitsPerSample = 24;
                target.Samples.wValidBitsPerSample = 24;
                target.SubFormat = GUID_KSDATAFORMAT_SUBTYPE_PCM;
                break;
            case SampleType::SINT_32:
                target.Format.wBitsPerSample = 32;
                target.Samples.wValidBitsPerSample = 32;
                target.SubFormat = GUID_KSDATAFORMAT_SUBTYPE_PCM;
                break;
            case SampleType::FLOAT_32:
                target.Format.wBitsPerSample = 32;
                target.Samples.wValidBitsPerSample = 32;
                target.SubFormat = GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
                break;
            case SampleType::FLOAT_64:
                target.Format.wBitsPerSample = 64;
                target.Samples.wValidBitsPerSample = 64;
                target.SubFormat = GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
                break;
            default:
                target.Format.wFormatTag = 0;
                break;
        }
    }

    return true;
}
bool AsioBackend::init() {
    if (ASIO_BACKEND) {
        log_warning("audio::asio", "ASIO callbacks already initialized");
        return false;
    }
    ASIO_BACKEND = this;

    // create buffer objects
    for (long i = 0; i < this->format_.Format.nChannels; i++) {
        auto &buffer_info = this->asio_buffers.emplace_back();
        buffer_info.is_input = AsioFalse;
        buffer_info.channel_num = i;
        buffer_info.buffers[0] = nullptr;
        buffer_info.buffers[1] = nullptr;
    }

    // setup ASIO callbacks
    this->asio_callbacks.buffer_switch = AsioBackend::buffer_switch;
    this->asio_callbacks.sample_rate_did_change = AsioBackend::sample_rate_did_change;
    this->asio_callbacks.asio_message = AsioBackend::asio_message;

    // create buffers
    auto result = this->asio_driver->create_buffers(
        this->asio_buffers.data(),
        this->format_.Format.nChannels,
        this->asio_info_.buffer_preferred_size,
        &this->asio_callbacks);
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to create buffers: {}", asio_error_str(result));
        return false;
    }

    auto sample_size = sample_type_size(this->asio_sample_type);
    auto num_samples = static_cast<size_t>(this->asio_info_.buffer_preferred_size);
    auto buffer_size = num_samples * sample_size;

    for (const auto &buffer_info : this->asio_buffers) {
        for (size_t i = 0; i < _countof(buffer_info.buffers); i++) {
            log_misc("audio::asio", "channel[{}].buffers[{}] = {}",
                     buffer_info.channel_num,
                     i,
                     fmt::ptr(buffer_info.buffers[i]));

            // initialize buffer contents to avoid garbage from being played on start
            memset(buffer_info.buffers[i], 0, buffer_size);
        }
    }

    // FlexASIO throws an error if `update_latencies` is called before `create_buffers`
    if (!this->update_latency()) {
        return false;
    }

    return true;
}
bool AsioBackend::unload_driver() {
    AsioError result;

    if (!this->asio_driver) {
        return true;
    }

    // stop audio
    result = this->asio_driver->stop();
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to stop audio: {}", asio_error_str(result));
    }

    // clear buffer state
    this->asio_buffers.clear();

    // dispose buffers
    result = this->asio_driver->dispose_buffers();
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to dispose buffers: {}", asio_error_str(result));
    }

    // unload driver
    this->asio_driver->Release();
    this->asio_driver = nullptr;

    // reset global state
    if (ASIO_BACKEND == this) {
        ASIO_BACKEND = nullptr;
    }

    // reset local state
    this->asio_channel_info_.clear();
    memset(&this->asio_callbacks, 0, sizeof(this->asio_callbacks));
    memset(&this->driver_info_, 0, sizeof(this->driver_info_));
    memset(&this->asio_info_, 0, sizeof(this->asio_info_));
    this->asio_sample_type = SampleType::UNSUPPORTED;

    return true;
}
void AsioBackend::reset() {
    AsioError result;

    if (!this->unload_driver()) {
        log_warning("audio::asio", "failed to unload driver");
    }

    if (!this->load_driver() ||
        !this->update_driver_info() ||
        !this->set_initial_format(this->last_checked_format))
    {
        log_warning("audio::asio", "failed to initialize driver");
        this->set_thread_state(AsioThreadState::Failed);
        return;
    }

    auto sample_rate = this->format_.Format.nSamplesPerSec;

    result = this->asio_driver->set_sample_rate(static_cast<double>(sample_rate));
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to set sample rate: {}", asio_error_str(result));
        return;
    }

    // init buffers
    if (FAILED(this->init())) {
        return;
    }

    // start processing if started before reset
    if (this->started.load()) {
        result = this->asio_driver->start();
        if (result != ASE_OK) {
            log_warning("audio::asio", "failed to start processing: {}", asio_error_str(result));
            return;
        }
    }
}
AsioError AsioBackend::run_on_asio_thread(AsioFunction fn, bool result_needed) {
    AsioError result = ASE_NotPresent;

    struct AsioThreadMessage msg {
        .fn = std::move(fn),
        .result_needed = result_needed,
    };
    this->asio_msg_queue_func.enqueue(msg);

    if (result_needed) {
        this->asio_msg_queue_result.wait_dequeue(result);
    }

    return result;
}
void AsioBackend::open_control_panel() {
    if (!this->asio_thread_initialized) {
        return;
    }

    auto result = this->run_on_asio_thread([this]() {
        return this->asio_driver->control_panel();
    });
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to open control panel: {}", asio_error_str(result));
    }
}

void AsioBackend::buffer_switch(long double_buffer_index, AsioBool) {
    auto self = ASIO_BACKEND;

    auto sample_size = sample_type_size(self->asio_sample_type);
    auto channels = static_cast<size_t>(self->format_.Format.nChannels);
    auto num_samples = static_cast<size_t>(self->asio_info_.buffer_preferred_size);
    auto frame_size = channels * sample_size;
    auto buffer_len = num_samples * frame_size;
    size_t written = 0;

    struct BufferEntry *entry;
    while ((entry = self->queue.peek()) != nullptr) {
        auto write_len = std::min(entry->length - entry->read, buffer_len - written);

        if (write_len == 0) {
            break;
        }

        auto frames_to_write = write_len / frame_size;

        for (size_t i = 0; i < frames_to_write; i++) {
            for (size_t j = 0; j < channels; j++) {
                auto buffer = reinterpret_cast<uint8_t *>(self->asio_buffers[j].buffers[double_buffer_index]);
                memcpy(
                        &buffer[i * sample_size],
                        &entry->buffer[entry->read + (i * channels + j) * sample_size],
                        sample_size);
            }
        }

        /*
        if (frames_to_write * frame_size != write_len) {
            log_warning("audio::asio", "dropped some frames!");
        }
        */

        written += write_len;

        if (entry->read + write_len >= entry->length) {
            CoTaskMemFree(entry->buffer);
            self->queue.pop();
        } else {
            entry->read += write_len;
        }
    }

    // not all drivers support this method, ignore error
    self->asio_driver->output_ready();

    if (self->relay_handle.has_value()) {

        // trigger game audio callback
        if (!SetEvent(self->relay_handle.value())) {
            DWORD last_error = GetLastError();

            log_warning("audio::asio", "AsioBackend::buffer_switch: SetEvent failed: {} ({})",
                    last_error,
                    std::system_category().message(last_error));
        }
    }

    if (written > 0) {
        self->queued_frames.fetch_sub(static_cast<uint32_t>(written / frame_size));
        self->queued_bytes.fetch_sub(written);
    }
}
void AsioBackend::sample_rate_did_change(AsioSampleRate sample_rate) {
    auto self = ASIO_BACKEND;

    log_warning("audio::asio", "sample rate change to {} detected, not supported, resetting",
            sample_rate);

    self->run_on_asio_thread([self]() {
        self->reset();

        return ASE_OK;
    }, false);
}
long AsioBackend::asio_message(long selector, long value, void *message, double *opt) {
    auto self = ASIO_BACKEND;

    switch (selector) {
        case kAsioSelectorSupported:
            switch (value) {
                case kAsioEngineVersion:
                case kAsioResetRequest:
                case kAsioResyncRequest:
                case kAsioLatenciesChanged:
                case kAsioSupportsTimeInfo:
                case kAsioOverload:
                    return 1L;
                default:
                    return 0L;
            }
        case kAsioEngineVersion:
            return 2L;
        case kAsioResetRequest:
            log_misc("audio::asio", "reset request");

            self->run_on_asio_thread([self]() {
                self->reset();

                return ASE_OK;
            }, false);

            return 1L;
        case kAsioResyncRequest:
            log_misc("audio::asio", "resyncing");
            return 1L;
        case kAsioLatenciesChanged:
            log_misc("audio::asio", "latency changed");

            self->run_on_asio_thread([self]() {
                self->update_latency();

                return ASE_OK;
            }, false);

            return 0L;
        case kAsioSupportsTimeInfo:
            // TODO(felix): support timecode buffer switch
            return 0L;
        case kAsioOverload:
            log_misc("audio::asio", "overload detected");
            return 1L;
        default:
            return 0L;
    }
}

bool AsioBackend::is_supported_subformat(const WAVEFORMATEXTENSIBLE &format_ex) noexcept {
    bool pcm_format = false;
    bool float_format = false;

    const auto &format = format_ex.Format;

    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        pcm_format = true;
    } else if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        float_format = true;
    } else if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (format_ex.SubFormat == GUID_KSDATAFORMAT_SUBTYPE_PCM) {
            pcm_format = true;
        } else if (format_ex.SubFormat == GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            float_format = true;
        }
    } else {
        log_warning("audio::asio", "unsupported format tag");
        return false;
    }

    if (pcm_format) {
        switch (format.wBitsPerSample) {
            case 16:
            case 24:
            case 32:
                break;
            default:
                log_warning("audio::asio", "unknown PCM sample size: {}", format.wBitsPerSample);
                return false;
        }
    } else if (float_format) {
        switch (format.wBitsPerSample) {
            case 32:
            case 64:
                break;
            default:
                log_warning("audio::asio", "unknown float sample size: {}", format.wBitsPerSample);
                return false;
        }
    }

    return true;
}
REFERENCE_TIME AsioBackend::compute_ref_time() const {
    auto sample_rate = this->last_checked_format.Format.nSamplesPerSec;
    auto buffer_frames = this->asio_info_.buffer_preferred_size;

    return static_cast<REFERENCE_TIME>(ceil(REFTIMES_PER_SEC * buffer_frames / sample_rate));
}
REFERENCE_TIME AsioBackend::compute_latency_ref_time() const {
    auto sample_rate = this->last_checked_format.Format.nSamplesPerSec;
    auto buffer_frames = this->asio_info_.output_latency;

    return static_cast<REFERENCE_TIME>(ceil(REFTIMES_PER_SEC * buffer_frames / sample_rate));
}

const WAVEFORMATEXTENSIBLE &AsioBackend::format() const noexcept {
    return this->format_;
}

HRESULT AsioBackend::on_initialize(
    AUDCLNT_SHAREMODE *ShareMode,
    DWORD *StreamFlags,
    REFERENCE_TIME *hnsBufferDuration,
    REFERENCE_TIME *hnsPeriodicity,
    const WAVEFORMATEX *pFormat,
    LPCGUID AudioSessionGuid) noexcept
{
    AsioError result;

    copy_wave_format(&this->format_, pFormat);
    memcpy(&this->last_checked_format, &this->format_, sizeof(this->format_));

    if (!this->asio_thread_initialized) {
        log_warning("audio::asio", "{}: ASIO thread not initialized", "AsioBackend::on_initialize");
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    if (ASIO_BACKEND) {
        log_warning("audio::asio", "ASIO backend already initialized");
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }

    auto sample_rate = this->format_.Format.nSamplesPerSec;

    result = this->run_on_asio_thread([this, sample_rate]() {
        return this->asio_driver->set_sample_rate(static_cast<double>(sample_rate));
    });
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to set sample rate: {}", asio_error_str(result));
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    auto ref_time = this->compute_ref_time();
    log_info("audio::asio", "AsioBackend::on_intialize: sample rate = {}, reference time = {}",
             sample_rate,
             ref_time);

    // warn if this is being used on shared mode without event callback
    if (*ShareMode == AUDCLNT_SHAREMODE_SHARED &&
        (*StreamFlags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) != AUDCLNT_STREAMFLAGS_EVENTCALLBACK)
    {
        log_warning("audio::asio", "shared mode without event callback is not supported, sound will be garbled!");
    }

    /*
    // change to shared mode in case the audio dummy mode is not being used
    if (*ShareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
        *ShareMode = AUDCLNT_SHAREMODE_SHARED;
    }
    */

    *hnsBufferDuration = ref_time;
    *hnsPeriodicity = ref_time;

    if (!is_supported_subformat(this->format_)) {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    result = this->run_on_asio_thread([this]() {
        return this->init() ? ASE_OK : ASE_NotPresent;
    });
    if (result != ASE_OK) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    return S_OK;
}
HRESULT AsioBackend::on_get_buffer_size(uint32_t *buffer_frames) noexcept {
    *buffer_frames = static_cast<uint32_t>(this->asio_info_.buffer_preferred_size);

    return S_OK;
}
HRESULT AsioBackend::on_get_stream_latency(REFERENCE_TIME *latency) noexcept {
    if (!this->asio_thread_initialized) {
        log_warning("audio::asio", "{}: ASIO thread not initialized", "AsioBackend::on_get_stream_latency");
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    auto result = this->run_on_asio_thread([this]() {
        return this->update_latency() ? ASE_OK : ASE_NotPresent;
    });
    if (result != ASE_OK) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    auto latency_ref_time = this->compute_latency_ref_time();
    log_misc("audio::asio", "output latency = {}, reference time = {}",
            this->asio_info_.output_latency,
            latency_ref_time);

    *latency = latency_ref_time;

    return S_OK;
}
HRESULT AsioBackend::on_get_current_padding(std::optional<uint32_t> &padding_frames) noexcept {
    padding_frames = this->queued_frames.load();

    return S_OK;
}
HRESULT AsioBackend::on_is_format_supported(
    AUDCLNT_SHAREMODE *ShareMode,
    const WAVEFORMATEX *pFormat,
    WAVEFORMATEX **ppClosestMatch) noexcept
{
    if (!this->asio_thread_initialized) {
        log_warning("audio::asio", "{}: ASIO thread not initialized", "AsioBackend::on_is_format_supported");
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    copy_wave_format(&this->last_checked_format, pFormat);

    auto num_channels = this->last_checked_format.Format.nChannels;
    auto sample_rate = this->last_checked_format.Format.nSamplesPerSec;

    log_misc("audio::asio", "AsioBackend::on_is_format_supported: checking format {} channels, {} Hz, {}-bit",
            num_channels,
            sample_rate,
            this->last_checked_format.Format.wBitsPerSample);

    // check channel count
    if (num_channels > this->asio_info_.outputs) {
        log_warning("audio::asio", "channel count larger than number of device channels");
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    // check sub-format
    if (!this->is_supported_subformat(this->last_checked_format)) {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    // check sample rate
    auto result = this->run_on_asio_thread([this, sample_rate]() {
        return this->asio_driver->can_sample_rate(static_cast<double>(sample_rate));
    });
    if (result != ASE_OK) {
        log_warning("audio::asio", "unsupported sample rate: {}", sample_rate);
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    return S_OK;
}
HRESULT AsioBackend::on_get_mix_format(WAVEFORMATEX **pp_device_format) noexcept {
    if (!this->asio_thread_initialized) {
        log_warning("audio::asio", "{}: ASIO thread not initialized", "AsioBackend::on_get_mix_format");
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    auto format = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE)));

    if (!format) {
        DWORD last_error = GetLastError();

        log_warning("audio::asio", "failed to allocate memory for mix format: {} ({})",
                last_error,
                std::system_category().message(last_error));

        return AUDCLNT_E_BUFFER_ERROR;
    }

    // build format based on ASIO driver parameters
    auto result = this->run_on_asio_thread([this, format]() {
        return this->set_initial_format(*format) ? ASE_OK : ASE_NotPresent;
    });
    if (result != ASE_OK) {
        CoTaskMemFree(format);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    *pp_device_format = reinterpret_cast<WAVEFORMATEX *>(format);

    return S_OK;
}
HRESULT AsioBackend::on_get_device_period(
    REFERENCE_TIME *default_device_period,
    REFERENCE_TIME *minimum_device_period) noexcept
{
    auto ref_time = this->compute_ref_time();

    log_info("audio::asio", "AsioBackend::on_get_device_period: sample rate = {}, reference time = {}",
            this->last_checked_format.Format.nSamplesPerSec,
            ref_time);

    if (default_device_period) {
        *default_device_period = ref_time;
    }
    if (minimum_device_period) {
        *minimum_device_period = ref_time;
    }

    return S_OK;
}
HRESULT AsioBackend::on_start() noexcept {
    log_misc("audio::asio", "AsioBackend::on_start");

    if (!this->asio_thread_initialized) {
        log_warning("audio::asio", "{}: ASIO thread not initialized", "AsioBackend::on_start");
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    // FlexASIO creates the WASAPI context upon start, prevent recurive initialization
    std::lock_guard initialize_guard(hooks::audio::INITIALIZE_LOCK);

    auto result = this->run_on_asio_thread([this]() {
        return this->asio_driver->start();
    });
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to start processing: {}", asio_error_str(result));
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    this->started = true;

    // wait until all queued frames are dequeued
    while (this->queued_frames > 0) {
        std::this_thread::yield();
    }

    return S_OK;
}
HRESULT AsioBackend::on_stop() noexcept {
    log_misc("audio::asio", "AsioBackend::on_stop");

    if (!this->asio_thread_initialized) {
        log_warning("audio::asio", "{}: ASIO thread not initialized", "AsioBackend::on_stop");
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if (hooks::audio::ASIO_FORCE_UNLOAD_ON_STOP) {
        log_misc("audio::asio", "AsioBackend::on_stop - using ASIO_FORCE_UNLOAD_ON_STOP workaround");
    }

    auto result = this->run_on_asio_thread([this]() {
        if (hooks::audio::ASIO_FORCE_UNLOAD_ON_STOP) {
            this->unload_driver();
            return (AsioError)ASE_OK;
        }
        return this->asio_driver->stop();
    });
    if (result != ASE_OK) {
        log_warning("audio::asio", "failed to stop processing: {}", asio_error_str(result));
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    this->started = false;

    return S_OK;
}
HRESULT AsioBackend::on_set_event_handle(HANDLE *event_handle) noexcept {
    this->relay_handle = *event_handle;

    // give WASAPI a dummy handle
    *event_handle = CreateEvent(nullptr, true, false, nullptr);

    return S_OK;
}
HRESULT AsioBackend::on_get_buffer(uint32_t num_frames_requested, BYTE **pp_data) noexcept {
    const size_t buffer_size = this->format_.Format.nBlockAlign * num_frames_requested;

    // account for larger conversion buffer size
    const auto channels = static_cast<size_t>(this->format_.Format.nChannels);
    const auto sample_type = this->asio_sample_type;
    const auto converted_size = required_buffer_size(num_frames_requested, channels, sample_type);

    const size_t max_size = std::max(buffer_size, converted_size);

    // allocate temporary sound buffer
    this->active_sound_buffer = reinterpret_cast<BYTE *>(CoTaskMemAlloc(max_size));

    // check for allocation error
    if (!this->active_sound_buffer) {
        DWORD last_error = GetLastError();

        log_warning("audio::asio", "failed to allocate sound buffer: {} ({})",
                last_error,
                std::system_category().message(last_error));

        return AUDCLNT_E_BUFFER_ERROR;
    }

    // hand the buffer to the callee
    *pp_data = this->active_sound_buffer;

    return S_OK;
}
HRESULT AsioBackend::on_release_buffer(uint32_t num_frames_written, DWORD flags) noexcept {
    const size_t length = this->format_.Format.nBlockAlign * num_frames_written;

    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == AUDCLNT_BUFFERFLAGS_SILENT) {
        memset(this->active_sound_buffer, 0, length);
    }

    /*
    if (this->last_sound_buffer.size() < length) {
        this->last_sound_buffer.resize(length);
    }

    memcpy(this->last_sound_buffer.data(), this->active_sound_buffer, length);
    */

    // compute the buffer size after conversion
    const auto channels = this->format_.Format.nChannels;
    const auto sample_type = this->asio_sample_type;
    const size_t conversion_size = required_buffer_size(num_frames_written, channels, sample_type);

    // audio channel deinterleaving and subformat conversion
    convert_sample_type(
            channels,
            reinterpret_cast<uint8_t *>(this->active_sound_buffer),
            length,
            this->conversion_sound_buffer,
            convert_windows_format(this->format_),
            sample_type);

    // enqueue the buffer for playback
    struct BufferEntry entry {
        .buffer = this->active_sound_buffer,
        .length = conversion_size,
        .read = 0,
    };
    this->queue.enqueue(entry);
    this->queued_frames.fetch_add(num_frames_written);
    this->queued_bytes.fetch_add(conversion_size);

    return S_OK;
}
