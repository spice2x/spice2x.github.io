#include "asio_proxy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "external/asio/asiolist.h"
#include "hooks/audio/audio.h"
#include "util/logging.h"
#include "util/utils.h"

namespace {

    // readable name for an ASIO sample type (e.g. "ASIOSTInt32LSB"), falling back to the
    // numeric value for unknown types
    const char *asio_sample_type_name(AsioSampleType type) {
        switch (type) {
            case ASIOSTInt16MSB: return "ASIOSTInt16MSB";
            case ASIOSTInt24MSB: return "ASIOSTInt24MSB";
            case ASIOSTInt32MSB: return "ASIOSTInt32MSB";
            case ASIOSTFloat32MSB: return "ASIOSTFloat32MSB";
            case ASIOSTFloat64MSB: return "ASIOSTFloat64MSB";
            case ASIOSTInt32MSB16: return "ASIOSTInt32MSB16";
            case ASIOSTInt32MSB18: return "ASIOSTInt32MSB18";
            case ASIOSTInt32MSB20: return "ASIOSTInt32MSB20";
            case ASIOSTInt32MSB24: return "ASIOSTInt32MSB24";
            case ASIOSTInt16LSB: return "ASIOSTInt16LSB";
            case ASIOSTInt24LSB: return "ASIOSTInt24LSB";
            case ASIOSTInt32LSB: return "ASIOSTInt32LSB";
            case ASIOSTFloat32LSB: return "ASIOSTFloat32LSB";
            case ASIOSTFloat64LSB: return "ASIOSTFloat64LSB";
            case ASIOSTInt32LSB16: return "ASIOSTInt32LSB16";
            case ASIOSTInt32LSB18: return "ASIOSTInt32LSB18";
            case ASIOSTInt32LSB20: return "ASIOSTInt32LSB20";
            case ASIOSTInt32LSB24: return "ASIOSTInt32LSB24";
            default: return "unknown";
        }
    }

    // bytes occupied by one sample of the given ASIO type, or 0 for formats we cannot size
    // (used to compute the byte length of a planar channel buffer for raw copies)
    int asio_sample_bytes(AsioSampleType type) {
        switch (type) {
            case ASIOSTInt16LSB:
            case ASIOSTInt16MSB:
                return 2;
            case ASIOSTInt24LSB:
            case ASIOSTInt24MSB:
                return 3;
            case ASIOSTInt32LSB:
            case ASIOSTInt32MSB:
            case ASIOSTInt32LSB16:
            case ASIOSTInt32LSB18:
            case ASIOSTInt32LSB20:
            case ASIOSTInt32LSB24:
            case ASIOSTInt32MSB16:
            case ASIOSTInt32MSB18:
            case ASIOSTInt32MSB20:
            case ASIOSTInt32MSB24:
            case ASIOSTFloat32LSB:
            case ASIOSTFloat32MSB:
                return 4;
            case ASIOSTFloat64LSB:
            case ASIOSTFloat64MSB:
                return 8;
            default:
                return 0;
        }
    }

    // readable name for a stereo downmix selection, used in our logs
    const char *stereo_downmix_name(WrappedAsio::StereoDownmix mode) {
        switch (mode) {
            case WrappedAsio::StereoDownmix::None: return "none";
            case WrappedAsio::StereoDownmix::Front: return "front";
            case WrappedAsio::StereoDownmix::Center: return "center";
            case WrappedAsio::StereoDownmix::Rear: return "rear";
            case WrappedAsio::StereoDownmix::Side: return "side";
            default: return "unknown";
        }
    }

    // duration in milliseconds of a buffer of the given frame count at a sample rate, or a
    // negative sentinel when the frame count or sample rate is unusable
    double frames_to_ms(long frames, AsioSampleRate sample_rate) {
        if (frames < 0 || sample_rate <= 0.0) {
            return -1.0;
        }
        return (frames * 1000.0) / sample_rate;
    }

    // scales one planar ASIO output buffer (frames samples of the given type) by gain in
    // place, clamping integer formats so a boost saturates instead of wrapping. unsupported
    // formats are left untouched. runs on the driver's realtime thread, so no allocation,
    // locking or logging here
    void apply_gain_planar(void *buffer, long frames, AsioSampleType type, float gain) {
        if (buffer == nullptr || frames <= 0) {
            return;
        }
        switch (type) {
            case ASIOSTFloat32LSB: {
                auto p = static_cast<float *>(buffer);
                for (long i = 0; i < frames; i++) {
                    p[i] = std::clamp(p[i] * gain, -1.0f, 1.0f);
                }
                break;
            }
            case ASIOSTFloat64LSB: {
                auto p = static_cast<double *>(buffer);
                for (long i = 0; i < frames; i++) {
                    p[i] = std::clamp(p[i] * static_cast<double>(gain), -1.0, 1.0);
                }
                break;
            }
            case ASIOSTInt16LSB: {
                auto p = static_cast<int16_t *>(buffer);
                for (long i = 0; i < frames; i++) {
                    p[i] = static_cast<int16_t>(
                        std::clamp(std::lround(p[i] * gain), -32768L, 32767L));
                }
                break;
            }
            case ASIOSTInt24LSB: {
                // packed 24-bit little-endian, 3 bytes per sample
                auto bytes = static_cast<uint8_t *>(buffer);
                for (long i = 0; i < frames; i++) {
                    uint8_t *s = bytes + i * 3;
                    int32_t v = s[0] | (s[1] << 8) | (s[2] << 16);
                    if (v & 0x800000) {
                        v |= ~0xFFFFFF; // sign extend
                    }
                    int64_t scaled = std::clamp<int64_t>(
                        std::llround(static_cast<double>(v) * gain), -8388608, 8388607);
                    s[0] = scaled & 0xFF;
                    s[1] = (scaled >> 8) & 0xFF;
                    s[2] = (scaled >> 16) & 0xFF;
                }
                break;
            }
            case ASIOSTInt32LSB: {
                auto p = static_cast<int32_t *>(buffer);
                for (long i = 0; i < frames; i++) {
                    p[i] = static_cast<int32_t>(std::clamp<int64_t>(
                        std::llround(static_cast<double>(p[i]) * gain), INT32_MIN, INT32_MAX));
                }
                break;
            }
            default:
                // unsupported format (MSB, aligned 32-bit, DSD): leave untouched
                break;
        }
    }

    // persistent wrapper per CLSID. ASIO drivers are single-instance, and some hardware
    // drivers (e.g. Neva Uno) crash if their COM object is destroyed and re-created within
    // a process - which the host triggers by leaking and re-instantiating the driver during
    // startup probing. so we build the real driver and its wrapper once, hold a reference so
    // it survives the host's Release calls, and hand the same wrapper back for every later
    // CoCreate. as a result the real driver is created and initialized exactly once
    std::mutex g_wrappers_mutex;
    std::vector<std::pair<CLSID, WrappedAsio *>> g_persistent_wrappers;

    WrappedAsio *find_persistent_wrapper(REFCLSID clsid) {
        std::lock_guard lock(g_wrappers_mutex);
        for (auto &entry : g_persistent_wrappers) {
            if (IsEqualCLSID(entry.first, clsid)) {
                return entry.second;
            }
        }

        return nullptr;
    }

    void store_persistent_wrapper(REFCLSID clsid, WrappedAsio *wrapper) {
        std::lock_guard lock(g_wrappers_mutex);
        g_persistent_wrappers.emplace_back(clsid, wrapper);
    }

    // ASIO drivers registered on this system (CLSID + registry name), scanned once
    const std::vector<std::pair<CLSID, std::string>> &registered_asio_drivers() {
        static const std::vector<std::pair<CLSID, std::string>> drivers = [] {
            std::vector<std::pair<CLSID, std::string>> result;
            AsioDriverList driver_list;
            for (const auto &driver : driver_list.driver_list) {
                result.emplace_back(driver.clsid, driver.name);
                log_info(
                    "audio::wrappedasio",
                    "registered ASIO driver: name='{}', clsid={}",
                    driver.name,
                    guid2s(driver.clsid));
            }

            log_info("audio::wrappedasio", "discovered {} registered ASIO driver(s)", result.size());
            return result;
        }();

        return drivers;
    }

    std::string registered_asio_name(REFCLSID clsid) {
        for (const auto &driver : registered_asio_drivers()) {
            if (IsEqualCLSID(driver.first, clsid)) {
                return driver.second;
            }
        }

        return guid2s(clsid);
    }
}

namespace hooks::audio::asio {

    bool is_asio_creation(REFCLSID rclsid, REFIID riid) {

        // ASIO hosts request the driver using its own CLSID as the interface id
        if (!IsEqualGUID(rclsid, riid)) {
            return false;
        }

        for (const auto &driver : registered_asio_drivers()) {
            if (IsEqualCLSID(driver.first, rclsid)) {
                return true;
            }
        }

        return false;
    }
}

#pragma region IUnknown
WrappedAsio::StereoDownmix WrappedAsio::STEREO_DOWNMIX = WrappedAsio::StereoDownmix::None;
std::atomic<WrappedAsio *> WrappedAsio::active_instance {nullptr};

WrappedAsio::StereoDownmix WrappedAsio::name_to_stereo_downmix(const char *name) {
    if (_stricmp(name, "front") == 0) {
        return StereoDownmix::Front;
    } else if (_stricmp(name, "center") == 0) {
        return StereoDownmix::Center;
    } else if (_stricmp(name, "rear") == 0) {
        return StereoDownmix::Rear;
    } else if (_stricmp(name, "side") == 0) {
        return StereoDownmix::Side;
    }

    return StereoDownmix::None;
}

WrappedAsio::~WrappedAsio() {
    this->detach_post_process();

    // we hold a reference for the process lifetime, so this only runs at teardown. release
    // the real driver for completeness; in practice the process is already exiting and the
    // OS reclaims it - we never tear it down mid-run (that is what crashes some drivers)
    this->pReal->Release();

    log_info("audio::wrappedasio", "destroying wrapped ASIO driver, clsid={}", guid2s(this->clsid));
}

HRESULT STDMETHODCALLTYPE WrappedAsio::QueryInterface(REFIID riid, void **ppv) {
    if (ppv == nullptr) {
        return E_POINTER;
    }

    // ASIO hosts query for the driver using its own CLSID as the IID
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, this->clsid)) {
        this->AddRef();
        *ppv = static_cast<IAsio *>(this);

        return S_OK;
    }

    // the host is asking for some other interface; forward to the real driver. a failure
    // here is a common reason a host discards a driver and retries
    const HRESULT ret = this->pReal->QueryInterface(riid, ppv);
    if (SUCCEEDED(ret)) {
        log_info("audio::wrappedasio", "QueryInterface({}) -> forwarded to real driver", guid2s(riid));
    } else {
        log_info(
            "audio::wrappedasio",
            "QueryInterface({}) -> not supported by driver, hr={:#x}",
            guid2s(riid),
            static_cast<unsigned long>(ret));
    }

    return ret;
}

ULONG STDMETHODCALLTYPE WrappedAsio::AddRef() {
    // decoupled from the real driver: we count host references on the wrapper and hold a
    // single reference on pReal for our lifetime. this neutralizes a host bug (iidx32+)
    // that takes a duplicate AddRef with no matching Release, which would leak the driver
    return ++this->ref_count;
}

ULONG STDMETHODCALLTYPE WrappedAsio::Release() {
    const ULONG refs = --this->ref_count;
    if (refs == 0) {
        delete this;
    }

    return refs;
}
#pragma endregion

#pragma region IAsio
AsioBool __thiscall WrappedAsio::init(void *sys_handle) {
    // the real driver is single-instance and persistent; initialize it exactly once. the
    // host re-calls init() on each CoCreate during startup probing, but re-initializing a
    // live driver is undefined and crashes some hardware drivers, so treat a repeat with the
    // same window handle as success without touching the real driver
    if (this->initialized && this->init_handle == sys_handle) {
        log_misc("audio::wrappedasio", "init skipped, '{}' already initialized", this->driver_name);
        return AsioTrue;
    }

    const AsioBool result = this->pReal->init(sys_handle);
    if (result == AsioTrue) {
        this->initialized = true;
        this->init_handle = sys_handle;
        log_info(
            "audio::wrappedasio",
            "init succeeded for '{}' (driver version {})",
            this->driver_name,
            this->pReal->get_driver_version());
    } else {
        char message[128] = {};
        this->pReal->get_error_message(message);
        log_warning("audio::wrappedasio", "init failed: {}", message);
    }

    return result;
}

void __thiscall WrappedAsio::get_driver_name(char *name) {
    this->pReal->get_driver_name(name);
}

long __thiscall WrappedAsio::get_driver_version() {
    return this->pReal->get_driver_version();
}

void __thiscall WrappedAsio::get_error_message(char *string) {
    this->pReal->get_error_message(string);
}

AsioError __thiscall WrappedAsio::start() {
    const AsioError result = this->pReal->start();
    if (result == ASE_OK) {
        log_info(
            "audio::wrappedasio",
            "start succeeded, ASIO stream is now running on '{}'",
            this->driver_name);
    } else {
        log_warning("audio::wrappedasio", "start failed, err={}", static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::stop() {
    const AsioError result = this->pReal->stop();
    if (result == ASE_OK) {
        log_info("audio::wrappedasio", "stop succeeded, ASIO stream on '{}' halted", this->driver_name);
    } else {
        log_warning("audio::wrappedasio", "stop failed, err={}", static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::get_channels(long *num_input_channels, long *num_output_channels) {
    const AsioError result = this->pReal->get_channels(num_input_channels, num_output_channels);
    if (result != ASE_OK) {
        log_warning("audio::wrappedasio", "get_channels failed, err={}", static_cast<long>(result));
        return result;
    }

    if (force_two_channels()
        && num_output_channels != nullptr
        && *num_output_channels < FORCED_OUTPUT_CHANNELS)
    {
        // the device has fewer outputs than the game hardcodes; report the count it
        // expects so it proceeds to create_buffers, where we forward only the real
        // front pair and discard the rest
        log_info(
            "audio::wrappedasio",
            "reporting output channel count as {} (device has {}) for forced two-channel",
            FORCED_OUTPUT_CHANNELS,
            *num_output_channels);
        *num_output_channels = FORCED_OUTPUT_CHANNELS;
    }

    log_info(
        "audio::wrappedasio",
        "get_channels -> in={}, out={}",
        num_input_channels ? *num_input_channels : -1,
        num_output_channels ? *num_output_channels : -1);

    return result;
}

AsioError __thiscall WrappedAsio::get_latencies(long *input_latency, long *output_latency) {
    const AsioError result = this->pReal->get_latencies(input_latency, output_latency);
    if (result == ASE_OK) {
        // include millisecond equivalents alongside the frame counts for readability
        AsioSampleRate sample_rate = 0.0;
        this->pReal->get_sample_rate(&sample_rate);
        const long in_frames = input_latency ? *input_latency : -1;
        const long out_frames = output_latency ? *output_latency : -1;
        log_info(
            "audio::wrappedasio",
            "get_latencies -> in={} frames ({:.2f} ms), out={} frames ({:.2f} ms)",
            in_frames,
            frames_to_ms(in_frames, sample_rate),
            out_frames,
            frames_to_ms(out_frames, sample_rate));
    } else {
        log_warning("audio::wrappedasio", "get_latencies failed, err={}", static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::get_buffer_size(
    long *min_size,
    long *max_size,
    long *preferred_size,
    long *granularity)
{
    const AsioError result = this->pReal->get_buffer_size(min_size, max_size, preferred_size, granularity);
    if (result != ASE_OK) {
        log_warning("audio::wrappedasio", "get_buffer_size failed, err={}", static_cast<long>(result));
        return result;
    }

    // include millisecond equivalents alongside the frame counts for readability
    AsioSampleRate sample_rate = 0.0;
    this->pReal->get_sample_rate(&sample_rate);
    const long min_frames = min_size ? *min_size : -1;
    const long max_frames = max_size ? *max_size : -1;
    const long preferred_frames = preferred_size ? *preferred_size : -1;
    log_info(
        "audio::wrappedasio",
        "get_buffer_size -> min={} frames ({:.2f} ms), max={} frames ({:.2f} ms), "
        "preferred={} frames ({:.2f} ms), granularity={}",
        min_frames,
        frames_to_ms(min_frames, sample_rate),
        max_frames,
        frames_to_ms(max_frames, sample_rate),
        preferred_frames,
        frames_to_ms(preferred_frames, sample_rate),
        granularity ? *granularity : -1);

    return result;
}

AsioError __thiscall WrappedAsio::can_sample_rate(AsioSampleRate sample_rate) {
    const AsioError result = this->pReal->can_sample_rate(sample_rate);
    if (result == ASE_OK) {
        log_misc("audio::wrappedasio", "can_sample_rate({} Hz) -> supported", sample_rate);
    } else {
        log_misc(
            "audio::wrappedasio",
            "can_sample_rate({} Hz) -> not supported, err={}",
            sample_rate,
            static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::get_sample_rate(AsioSampleRate *sample_rate) {
    const AsioError result = this->pReal->get_sample_rate(sample_rate);
    if (result == ASE_OK) {
        log_misc("audio::wrappedasio", "get_sample_rate -> {} Hz", sample_rate ? *sample_rate : 0.0);
    } else {
        log_warning("audio::wrappedasio", "get_sample_rate failed, err={}", static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::set_sample_rate(AsioSampleRate sample_rate) {
    const AsioError result = this->pReal->set_sample_rate(sample_rate);
    if (result == ASE_OK) {
        log_info("audio::wrappedasio", "set_sample_rate({} Hz) succeeded", sample_rate);
    } else {
        log_warning(
            "audio::wrappedasio",
            "set_sample_rate({} Hz) failed, err={}",
            sample_rate,
            static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::get_clock_sources(ASIOClockSource *clocks, long *num_sources) {
    return this->pReal->get_clock_sources(clocks, num_sources);
}

AsioError __thiscall WrappedAsio::set_clock_source(long reference) {
    return this->pReal->set_clock_source(reference);
}

AsioError __thiscall WrappedAsio::get_sample_position(ASIOSamples *s_pos, ASIOTimeStamp *t_stamp) {
    return this->pReal->get_sample_position(s_pos, t_stamp);
}

AsioError __thiscall WrappedAsio::get_channel_info(AsioChannelInfo *info) {
    // forced two-channel: the game probes all output channels it thinks exist, but the
    // device only has the real front pair. fabricate a plausible entry for the channels
    // beyond the device without touching the real driver - they are discarded in
    // create_buffers anyway
    long real_in = 0, real_out = 0;
    if (force_two_channels()
        && info != nullptr
        && info->is_input == AsioFalse
        && this->pReal->get_channels(&real_in, &real_out) == ASE_OK
        && info->channel >= real_out)
    {
        const long channel = info->channel;

        // report the real device's output sample format rather than a fixed type: when
        // stereo downmix is active the game writes these dummy channels in this format and
        // we raw-copy the selected pair onto the device's front channels, so the formats
        // must match or the copy produces static. the guard above already proved the device
        // has output channels, so query channel 0's format directly (avoiding a redundant
        // get_channels) and fall back to Int32LSB if that query fails
        AsioChannelInfo real_ci {};
        real_ci.channel = 0;
        real_ci.is_input = AsioFalse;
        AsioSampleType fake_type = ASIOSTInt32LSB;
        if (this->pReal->get_channel_info(&real_ci) == ASE_OK) {
            fake_type = real_ci.type;
        }

        info->is_active = AsioTrue;
        info->channel_group = 0;
        info->type = fake_type;
        snprintf(info->name, sizeof(info->name), "Fake ASIO OUT %ld", channel);
        log_info(
            "audio::wrappedasio",
            "get_channel_info(channel={}, dir=output) -> fake channel, type={} ({})",
            channel,
            asio_sample_type_name(info->type),
            static_cast<long>(info->type));

        return ASE_OK;
    }

    const AsioError result = this->pReal->get_channel_info(info);
    if (result == ASE_OK && info != nullptr) {
        log_info(
            "audio::wrappedasio",
            "get_channel_info(channel={}, dir={}) -> active={}, group={}, type={} ({}), name='{}'",
            info->channel,
            info->is_input == AsioTrue ? "input" : "output",
            info->is_active == AsioTrue,
            info->channel_group,
            asio_sample_type_name(info->type),
            static_cast<long>(info->type),
            info->name);
    } else if (result != ASE_OK) {
        log_warning("audio::wrappedasio", "get_channel_info failed, err={}", static_cast<long>(result));
    }

    return result;
}

AsioCallbacks *WrappedAsio::install_proxy_callbacks(AsioCallbacks *game_callbacks) {
    const float gain = hooks::audio::VOLUME_BOOST;

    // start from a clean slate; a previous buffer set may have left state behind
    this->volume_channels.clear();
    this->volume_active = false;
    this->downmix_active = false;

    const bool want_volume = (gain != 1.0f);

    // front is the device's own pair, so selecting it (or None) means no copy is needed
    const bool want_downmix = (STEREO_DOWNMIX != StereoDownmix::None
        && STEREO_DOWNMIX != StereoDownmix::Front);

    // no post-processing configured (or no callbacks to wrap): pass the game's callbacks
    // straight through and do zero realtime work, exactly as before
    if ((!want_volume && !want_downmix) || game_callbacks == nullptr) {
        return game_callbacks;
    }

    if (want_volume) {
        this->volume_active = true;
        this->volume_gain = gain;
    }

    // the trampolines reach the game's buffer_switch through this copy, regardless of which
    // effect is active
    this->game_callbacks = *game_callbacks;

    // wrap only the buffer-switch callbacks, where the audio data lives and we rework it.
    // the other two carry no data we touch, so forward the game's own pointers unchanged -
    // the driver expects them non-null and the game already owns their context
    this->proxy_callbacks = {};
    this->proxy_callbacks.buffer_switch = &WrappedAsio::proxy_buffer_switch;
    this->proxy_callbacks.sample_rate_did_change = game_callbacks->sample_rate_did_change;
    this->proxy_callbacks.asio_message = game_callbacks->asio_message;
    this->proxy_callbacks.buffer_switch_time_info =
        game_callbacks->buffer_switch_time_info ? &WrappedAsio::proxy_buffer_switch_time_info : nullptr;

    return &this->proxy_callbacks;
}

AsioSampleType WrappedAsio::device_output_sample_type() {
    long real_in = 0, real_out = 0;
    if (this->pReal->get_channels(&real_in, &real_out) != ASE_OK || real_out <= 0) {
        return ASIOSTLastEntry;
    }

    AsioChannelInfo ci {};
    ci.channel = 0;
    ci.is_input = AsioFalse;
    if (this->pReal->get_channel_info(&ci) != ASE_OK) {
        return ASIOSTLastEntry;
    }
    return ci.type;
}

void WrappedAsio::record_volume_output_channel(const AsioBufferInfo &info) {
    if (info.is_input != AsioFalse) {
        return;
    }

    // ask the real driver for this channel's sample format so the realtime path knows how
    // to scale it; fall back to a sentinel that apply_gain_planar leaves untouched
    AsioChannelInfo ci {};
    ci.channel = info.channel_num;
    ci.is_input = AsioFalse;
    AsioSampleType type = ASIOSTLastEntry;
    if (this->pReal->get_channel_info(&ci) == ASE_OK) {
        type = ci.type;
    }

    VolumeOutputChannel ch;
    ch.buffers[0] = info.buffers[0];
    ch.buffers[1] = info.buffers[1];
    ch.type = type;
    this->volume_channels.push_back(ch);
}

void WrappedAsio::record_downmix_channels(
    AsioBufferInfo *buffer_infos, long num_channels, long buffer_size)
{
    // map the selected pair to source channel indices (0-indexed, standard 7.1 layout);
    // None and Front need no copy
    long src_left = 0, src_right = 0;
    switch (STEREO_DOWNMIX) {
        case StereoDownmix::Center: src_left = 2; src_right = 2; break;
        case StereoDownmix::Rear: src_left = 4; src_right = 5; break;
        case StereoDownmix::Side: src_left = 6; src_right = 7; break;
        default: return;
    }

    // find the double-buffer pair for a given output channel among those the game created,
    // or nullptr if the device does not expose it
    auto find_output = [&](long channel) -> void ** {
        for (long i = 0; i < num_channels; i++) {
            AsioBufferInfo &bi = buffer_infos[i];
            if (bi.is_input == AsioFalse && bi.channel_num == channel) {
                return bi.buffers;
            }
        }
        return nullptr;
    };

    // destinations are device channels 0/1; sources are the selected pair
    void **dst0 = find_output(0);
    void **dst1 = find_output(1);
    void **src_l = find_output(src_left);
    void **src_r = find_output(src_right);
    if (dst0 == nullptr || dst1 == nullptr || src_l == nullptr || src_r == nullptr) {
        log_warning(
            "audio::wrappedasio",
            "stereo downmix disabled: device is missing the front pair or source channels "
            "{}/{} (game created {} channel(s))",
            src_left,
            src_right,
            num_channels);
        return;
    }

    // all device output channels share one sample format; query it to size the copy
    const AsioSampleType type = this->device_output_sample_type();
    const int sample_bytes = asio_sample_bytes(type);
    if (sample_bytes <= 0) {
        log_warning(
            "audio::wrappedasio",
            "stereo downmix disabled: unsupported sample format {} ({})",
            asio_sample_type_name(type),
            static_cast<long>(type));
        return;
    }

    this->downmix_copies[0] = {{dst0[0], dst0[1]}, {src_l[0], src_l[1]}};
    this->downmix_copies[1] = {{dst1[0], dst1[1]}, {src_r[0], src_r[1]}};
    this->downmix_bytes = static_cast<size_t>(buffer_size) * sample_bytes;
    this->downmix_active = true;

    log_info(
        "audio::wrappedasio",
        "stereo downmix active: pair={} (src {}/{} -> device 0/1), {} frames, {} byte(s)/sample",
        stereo_downmix_name(STEREO_DOWNMIX),
        src_left,
        src_right,
        buffer_size,
        sample_bytes);
}

void WrappedAsio::publish_post_process(long buffer_size) {
    if (!this->volume_active && !this->downmix_active) {
        return;
    }

    // everything the realtime thread reads is now in place; make ourselves reachable
    this->volume_buffer_size = buffer_size;
    WrappedAsio::active_instance.store(this, std::memory_order_release);

    if (this->volume_active) {
        log_info(
            "audio::wrappedasio",
            "volume boost active: gain={}, scaling {} output channel(s)",
            this->volume_gain,
            this->volume_channels.size());
    }
}

void WrappedAsio::detach_post_process() {
    // stop our realtime trampolines from reaching this wrapper, but only if we are the
    // currently published instance
    WrappedAsio *expected = this;
    WrappedAsio::active_instance.compare_exchange_strong(expected, nullptr);
}

void WrappedAsio::apply_output_volume(long double_buffer_index) {
    if (double_buffer_index != 0 && double_buffer_index != 1) {
        return;
    }
    const float gain = this->volume_gain;
    const long frames = this->volume_buffer_size;
    for (const VolumeOutputChannel &ch : this->volume_channels) {
        apply_gain_planar(ch.buffers[double_buffer_index], frames, ch.type, gain);
    }
}

void WrappedAsio::apply_downmix(long double_buffer_index) {
    if (!this->downmix_active) {
        return;
    }
    if (double_buffer_index != 0 && double_buffer_index != 1) {
        return;
    }

    // raw planar copy of the selected source channels onto device channels 0/1; for the
    // center selection both copies share one source. skip self-copies (front)
    for (const DownmixCopy &copy : this->downmix_copies) {
        void *dst = copy.dst[double_buffer_index];
        const void *src = copy.src[double_buffer_index];
        if (dst != nullptr && src != nullptr && dst != src) {
            std::memcpy(dst, src, this->downmix_bytes);
        }
    }
}

void __cdecl WrappedAsio::proxy_buffer_switch(long double_buffer_index, AsioBool direct_process) {
    WrappedAsio *self = WrappedAsio::active_instance.load(std::memory_order_acquire);
    if (self == nullptr) {
        return;
    }

    // let the game write its samples into the driver buffers first, then rework them before
    // the driver plays this half on the next switch: downmix first (arrange channels 0/1),
    // then scale the device outputs by the volume boost
    if (self->game_callbacks.buffer_switch != nullptr) {
        self->game_callbacks.buffer_switch(double_buffer_index, direct_process);
    }
    self->apply_downmix(double_buffer_index);
    self->apply_output_volume(double_buffer_index);
}

AsioTime * __cdecl WrappedAsio::proxy_buffer_switch_time_info(
    AsioTime *params, long double_buffer_index, AsioBool direct_process)
{
    WrappedAsio *self = WrappedAsio::active_instance.load(std::memory_order_acquire);
    if (self == nullptr) {
        return params;
    }

    AsioTime *ret = params;
    if (self->game_callbacks.buffer_switch_time_info != nullptr) {
        ret = self->game_callbacks.buffer_switch_time_info(
            params, double_buffer_index, direct_process);
    } else if (self->game_callbacks.buffer_switch != nullptr) {
        self->game_callbacks.buffer_switch(double_buffer_index, direct_process);
    }
    self->apply_downmix(double_buffer_index);
    self->apply_output_volume(double_buffer_index);
    return ret;
}

AsioError __thiscall WrappedAsio::create_buffers(
    AsioBufferInfo *buffer_infos,
    long num_channels,
    long buffer_size,
    AsioCallbacks *callbacks)
{
    // swap in our buffer-switch trampolines if any post-processing is configured, so the
    // real driver calls us and we rework its output after the game fills it (no-op otherwise)
    AsioCallbacks *effective = this->install_proxy_callbacks(callbacks);

    if (force_two_channels()) {
        return this->create_buffers_front_pair(buffer_infos, num_channels, buffer_size, effective);
    }

    const AsioError result = this->pReal->create_buffers(buffer_infos, num_channels, buffer_size, effective);
    if (result == ASE_OK) {
        log_info(
            "audio::wrappedasio",
            "create_buffers(channels={}, size={} frames) succeeded",
            num_channels,
            buffer_size);

        // capture the post-process state now the buffers exist, then publish ourselves to
        // the realtime thread once everything is in place. downmix is not recorded here: any
        // active stereo extraction forces the front-pair path above, so this path only ever
        // runs the volume boost
        if (this->volume_active) {
            for (long i = 0; i < num_channels; i++) {
                this->record_volume_output_channel(buffer_infos[i]);
            }
        }
        this->publish_post_process(buffer_size);
    } else {
        log_warning(
            "audio::wrappedasio",
            "create_buffers(channels={}, size={} frames) failed, err={}",
            num_channels,
            buffer_size,
            static_cast<long>(result));
    }

    return result;
}

AsioError WrappedAsio::create_buffers_front_pair(
    AsioBufferInfo *buffer_infos,
    long num_channels,
    long buffer_size,
    AsioCallbacks *callbacks)
{
    // front-pair extraction (forced two-channel ASIO): the game asks for more output
    // channels than the real device has (e.g. 8 vs 2). forward only the channels the
    // device actually provides (channel 0/1 = front L/R) and hand the game throwaway
    // buffers for the rest, so its front mix lands on the device and the surround
    // channels are discarded. the game writes directly into the driver/dummy buffers
    // from its own bufferSwitch; the realtime work we do is the optional volume boost
    // (scaling the forwarded device channels) and, if a non-front stereo downmix is
    // selected, copying that pair from its dummy buffers onto the device's front pair
    long real_in = 0, real_out = 0;
    const AsioError ch_result = this->pReal->get_channels(&real_in, &real_out);
    if (ch_result != ASE_OK) {
        log_warning(
            "audio::wrappedasio",
            "create_buffers: get_channels failed, err={}",
            static_cast<long>(ch_result));
        return ch_result;
    }

    // partition the requested channels: those the device can serve are forwarded, the rest
    // are discarded. record source indices for both so we can patch the game's array after
    std::vector<AsioBufferInfo> forwarded;
    std::vector<long> forwarded_src;
    std::vector<long> discarded_src;
    forwarded.reserve(num_channels);
    forwarded_src.reserve(num_channels);
    discarded_src.reserve(num_channels);
    for (long i = 0; i < num_channels; i++) {
        const AsioBufferInfo &bi = buffer_infos[i];
        const long limit = (bi.is_input == AsioTrue) ? real_in : real_out;
        if (bi.channel_num < limit) {
            forwarded.push_back(bi);
            forwarded_src.push_back(i);
        } else {
            discarded_src.push_back(i);
        }
    }

    const AsioError result = this->pReal->create_buffers(
        forwarded.data(), static_cast<long>(forwarded.size()), buffer_size, callbacks);
    if (result != ASE_OK) {
        log_warning(
            "audio::wrappedasio",
            "create_buffers(forwarded={} of {}, size={} frames) failed, err={}",
            forwarded.size(),
            num_channels,
            buffer_size,
            static_cast<long>(result));
        return result;
    }

    // copy the real driver buffer pointers back into the game's array
    for (size_t k = 0; k < forwarded.size(); k++) {
        AsioBufferInfo &dst = buffer_infos[forwarded_src[k]];
        dst.buffers[0] = forwarded[k].buffers[0];
        dst.buffers[1] = forwarded[k].buffers[1];

        // only the forwarded channels reach the device, so those are the ones the volume
        // boost scales (the discarded channels go to throwaway buffers below)
        if (this->volume_active) {
            this->record_volume_output_channel(dst);
        }
    }

    // hand throwaway double buffers to the discarded channels. sized generously at
    // 8 bytes/sample (covers every ASIO sample type) so the game can never overrun them
    // regardless of the negotiated format
    this->dummy_buffers.clear();
    this->dummy_buffers.reserve(discarded_src.size() * 2);
    const size_t dummy_bytes = static_cast<size_t>(buffer_size) * 8;
    for (const long i : discarded_src) {
        for (void *&buffer : buffer_infos[i].buffers) {
            auto buf = std::make_unique<uint8_t[]>(dummy_bytes);
            std::memset(buf.get(), 0, dummy_bytes);
            buffer = buf.get();
            this->dummy_buffers.push_back(std::move(buf));
        }
    }

    // record the downmix source/destination buffers now the array is fully patched: the
    // selected pair (e.g. rear) lives in the dummy buffers above, the device front pair in
    // the forwarded driver buffers, so the realtime copy lands the chosen pair on the
    // device's 2.0 output. then publish ourselves to the realtime thread
    this->record_downmix_channels(buffer_infos, num_channels, buffer_size);
    this->publish_post_process(buffer_size);

    log_info(
        "audio::wrappedasio",
        "create_buffers: front-pair extraction - forwarded {} channel(s) to device, "
        "discarded {} (requested {}, size={} frames)",
        forwarded.size(),
        discarded_src.size(),
        num_channels,
        buffer_size);

    return ASE_OK;
}

AsioError __thiscall WrappedAsio::dispose_buffers() {
    // stop our realtime trampolines from touching buffers the driver is about to free
    this->detach_post_process();

    const AsioError result = this->pReal->dispose_buffers();
    this->dummy_buffers.clear();
    this->volume_channels.clear();
    this->volume_active = false;
    this->downmix_active = false;
    return result;
}

AsioError __thiscall WrappedAsio::control_panel() {
    return this->pReal->control_panel();
}

AsioError __thiscall WrappedAsio::future(long selector, void *opt) {
    return this->pReal->future(selector, opt);
}

AsioError __thiscall WrappedAsio::output_ready() {
    return this->pReal->output_ready();
}
#pragma endregion

namespace hooks::audio::asio {

    IUnknown *wrap(REFCLSID clsid, void *real) {
        log_info("audio::wrappedasio", "wrapping ASIO driver interface, clsid={}", guid2s(clsid));

        auto *wrapper = new WrappedAsio(
            reinterpret_cast<IAsio *>(real), clsid, registered_asio_name(clsid));

        // hold a reference so the wrapper - and the real driver it owns - persists for the
        // process lifetime, surviving the host's Release calls. every later CoCreate for
        // this CLSID gets this same instance back (see wrap_existing), so the real driver is
        // never destroyed and re-created, and is initialized exactly once
        wrapper->AddRef();
        store_persistent_wrapper(clsid, wrapper);

        return static_cast<IAsio *>(wrapper);
    }

    IUnknown *wrap_existing(REFCLSID clsid) {
        WrappedAsio *wrapper = find_persistent_wrapper(clsid);
        if (wrapper == nullptr) {
            return nullptr;
        }

        log_misc(
            "audio::wrappedasio",
            "reusing persistent ASIO driver instance, clsid={}",
            guid2s(clsid));

        // hand the host another reference to the one instance we keep alive
        wrapper->AddRef();
        return static_cast<IAsio *>(wrapper);
    }
}

