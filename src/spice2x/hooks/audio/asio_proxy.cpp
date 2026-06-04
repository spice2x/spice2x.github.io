#include "asio_proxy.h"

#include <mutex>
#include <vector>

#include "external/asio/asiolist.h"
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

    // live wrappers by CLSID. ASIO drivers are single-instance, so a host that creates a
    // new instance without releasing the old one has leaked it; we track this to tear the
    // stale one down
    std::mutex g_wrappers_mutex;
    std::vector<std::pair<CLSID, WrappedAsio *>> g_wrappers;

    // register a wrapper as the live instance for its CLSID, returning any stale wrapper it
    // replaces so the caller can tear it down outside the lock
    WrappedAsio *register_wrapper(REFCLSID clsid, WrappedAsio *wrapper) {
        std::lock_guard lock(g_wrappers_mutex);
        for (auto &entry : g_wrappers) {
            if (IsEqualCLSID(entry.first, clsid)) {
                WrappedAsio *stale = entry.second;
                entry.second = wrapper;
                return stale;
            }
        }

        g_wrappers.emplace_back(clsid, wrapper);
        return nullptr;
    }

    void unregister_wrapper(WrappedAsio *wrapper) {
        std::lock_guard lock(g_wrappers_mutex);
        for (auto it = g_wrappers.begin(); it != g_wrappers.end(); ++it) {
            if (it->second == wrapper) {
                g_wrappers.erase(it);
                return;
            }
        }
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
WrappedAsio::~WrappedAsio() {
    unregister_wrapper(this);

    // our refcount is decoupled from the host's (see AddRef/Release), so pReal's count is
    // exactly one here and this releases/unloads it deterministically
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
    const AsioBool result = this->pReal->init(sys_handle);
    if (result == AsioTrue) {
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
    if (result == ASE_OK) {
        log_info(
            "audio::wrappedasio",
            "get_channels -> in={}, out={}",
            num_input_channels ? *num_input_channels : -1,
            num_output_channels ? *num_output_channels : -1);
    } else {
        log_warning("audio::wrappedasio", "get_channels failed, err={}", static_cast<long>(result));
    }

    return result;
}

AsioError __thiscall WrappedAsio::get_latencies(long *input_latency, long *output_latency) {
    const AsioError result = this->pReal->get_latencies(input_latency, output_latency);
    if (result == ASE_OK) {
        log_info(
            "audio::wrappedasio",
            "get_latencies -> in={} frames, out={} frames",
            input_latency ? *input_latency : -1,
            output_latency ? *output_latency : -1);
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
    if (result == ASE_OK) {
        log_info(
            "audio::wrappedasio",
            "get_buffer_size -> min={} frames, max={} frames, preferred={} frames, granularity={}",
            min_size ? *min_size : -1,
            max_size ? *max_size : -1,
            preferred_size ? *preferred_size : -1,
            granularity ? *granularity : -1);
    } else {
        log_warning("audio::wrappedasio", "get_buffer_size failed, err={}", static_cast<long>(result));
    }

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

AsioError __thiscall WrappedAsio::create_buffers(
    AsioBufferInfo *buffer_infos,
    long num_channels,
    long buffer_size,
    AsioCallbacks *callbacks)
{
    const AsioError result = this->pReal->create_buffers(buffer_infos, num_channels, buffer_size, callbacks);
    if (result == ASE_OK) {
        log_info(
            "audio::wrappedasio",
            "create_buffers(channels={}, size={} frames) succeeded",
            num_channels,
            buffer_size);
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

AsioError __thiscall WrappedAsio::dispose_buffers() {
    return this->pReal->dispose_buffers();
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

        // if the host already had a live wrapper for this CLSID it leaked the previous
        // instance (ASIO is single-instance); tear it down now so the real driver is
        // released before the reinit. works around games (iidx32+) that ref twice but
        // deref once before re-initializing.
        //
        // FlexASIO 1.9 and many DAC ASIO drivers can't handle this; FlexASIO 1.10 and
        // Xonar AE can
        if (WrappedAsio *stale = register_wrapper(clsid, wrapper)) {
            log_info(
                "audio::wrappedasio",
                "host did not release previous instance for clsid={}, forcing teardown",
                guid2s(clsid));
            delete stale;
        }

        return static_cast<IAsio *>(wrapper);
    }
}
