#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "external/asio/asio.h"
#include "external/asio/iasiodrv.h"

namespace hooks::audio::asio {

    // returns true if a CoCreateInstance call is instantiating a registered ASIO driver.
    // ASIO hosts pass the driver CLSID as both class id and interface id; we also validate
    // it against the system's registered ASIO drivers to avoid false positives
    bool is_asio_creation(REFCLSID rclsid, REFIID riid);

    // wrap a real ASIO driver instance, taking ownership of the supplied reference, and
    // return a proxy that forwards every call to it
    IUnknown *wrap(REFCLSID clsid, void *real);
}

// transparent proxy around a real ASIO driver; a single place to intercept ASIO traffic
struct WrappedAsio final : IAsio {
    WrappedAsio(IAsio *real, REFCLSID clsid, std::string name)
        : pReal(real), clsid(clsid), driver_name(std::move(name)) {
    }

    WrappedAsio(const WrappedAsio &) = delete;
    WrappedAsio &operator=(const WrappedAsio &) = delete;

    virtual ~WrappedAsio();

    // when set, the proxy presents the game's expected multichannel layout to the host so
    // it proceeds to create_buffers, then forwards only the device's real front pair and
    // discards the rest (see create_buffers). set once at boot, before any wrapper exists,
    // so it needs no synchronization
    static bool FORCE_TWO_CHANNELS;

    // some games hardcode a multichannel ASIO output and bail before create_buffers if
    // get_channels reports fewer, so we report at least this many output channels when
    // FORCE_TWO_CHANNELS is active
    static constexpr long FORCED_OUTPUT_CHANNELS = 8;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAsio
    AsioBool __thiscall init(void *sys_handle) override;
    void __thiscall get_driver_name(char *name) override;
    long __thiscall get_driver_version() override;
    void __thiscall get_error_message(char *string) override;
    AsioError __thiscall start() override;
    AsioError __thiscall stop() override;
    AsioError __thiscall get_channels(long *num_input_channels, long *num_output_channels) override;
    AsioError __thiscall get_latencies(long *input_latency, long *output_latency) override;
    AsioError __thiscall get_buffer_size(
        long *min_size,
        long *max_size,
        long *preferred_size,
        long *granularity) override;
    AsioError __thiscall can_sample_rate(AsioSampleRate sample_rate) override;
    AsioError __thiscall get_sample_rate(AsioSampleRate *sample_rate) override;
    AsioError __thiscall set_sample_rate(AsioSampleRate sample_rate) override;
    AsioError __thiscall get_clock_sources(ASIOClockSource *clocks, long *num_sources) override;
    AsioError __thiscall set_clock_source(long reference) override;
    AsioError __thiscall get_sample_position(ASIOSamples *s_pos, ASIOTimeStamp *t_stamp) override;
    AsioError __thiscall get_channel_info(AsioChannelInfo *info) override;
    AsioError __thiscall create_buffers(
        AsioBufferInfo *buffer_infos,
        long num_channels,
        long buffer_size,
        AsioCallbacks *callbacks) override;
    AsioError __thiscall dispose_buffers() override;
    AsioError __thiscall control_panel() override;
    AsioError __thiscall future(long selector, void *opt) override;
    AsioError __thiscall output_ready() override;
#pragma endregion

private:
    // create_buffers implementation used when FORCE_TWO_CHANNELS is active: forwards only
    // the channels the real device has and hands the game throwaway buffers for the rest
    AsioError create_buffers_front_pair(
        AsioBufferInfo *buffer_infos,
        long num_channels,
        long buffer_size,
        AsioCallbacks *callbacks);

    IAsio *const pReal;
    const CLSID clsid;

    // registry name of the driver (not get_driver_name), used in our logs as a single
    // unambiguous name; constant for our lifetime
    std::string driver_name;

    // our own reference count; we hold one reference on pReal and release it when this
    // drops to zero
    std::atomic<ULONG> ref_count {1};

    // throwaway double buffers handed to the channels we discard when FORCE_TWO_CHANNELS
    // is active (see create_buffers). owned for the lifetime of the buffer set and freed
    // in dispose_buffers; only read by the game from its own bufferSwitch, never by us
    std::vector<std::unique_ptr<uint8_t[]>> dummy_buffers;
};
