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
    // return a proxy that forwards every call to it. also records it as the cached
    // instance for its CLSID so later CoCreate calls can reuse it (see wrap_existing)
    IUnknown *wrap(REFCLSID clsid, void *real);

    // if a cached wrapper already exists for this CLSID, return it (with an added
    // reference); otherwise nullptr to signal the caller to create the real driver and
    // wrap it. lets the host reuse one driver instance instead of re-instantiating it
    IUnknown *wrap_existing(REFCLSID clsid);

    // drop the process-lifetime references taken by wrap() so cached drivers can be released
    // at shutdown. only relinquishes our pin, so a real driver is torn down once the host
    // has released its own references too. call from a controlled shutdown point, never from
    // a static destructor (the driver DLL may already be unloaded)
    void release_all_wrappers();
}

// transparent proxy around a real ASIO driver; a single place to intercept ASIO traffic
struct WrappedAsio final : IAsio {
    WrappedAsio(IAsio *real, REFCLSID clsid, std::string name)
        : pReal(real), clsid(clsid), driver_name(std::move(name)) {
    }

    WrappedAsio(const WrappedAsio &) = delete;
    WrappedAsio &operator=(const WrappedAsio &) = delete;

    virtual ~WrappedAsio();

    // selects which source channel pair of a multichannel ASIO output reaches the device's
    // 2.0 front pair. when not None, the proxy presents the game's expected multichannel
    // layout to the host so it proceeds to create_buffers, then opens only a two-channel
    // stream on the real device and routes the selected pair onto it (see create_buffers).
    // Front is the plain "force two channel" case (forward the device's own front pair);
    // the others copy a different pair onto 0/1. assumes a standard 7.1 layout (0-indexed).
    // set once at boot, before any wrapper exists, so it needs no synchronization
    enum class StereoDownmix {
        None,   // feature disabled - full multichannel passthrough
        Front,  // channels 0/1 - the device front pair is forwarded as-is (no copy)
        Center, // channel 2 duplicated to both 0 and 1
        Rear,   // channels 4/5 -> 0/1
        Side,   // channels 6/7 -> 0/1
    };
    static StereoDownmix STEREO_DOWNMIX;

    // true when a stereo extraction is configured, i.e. the real device should open a 2.0
    // stream and only the selected pair should reach it. the former standalone
    // FORCE_TWO_CHANNELS flag is now just the Front case of this
    static bool force_two_channels() {
        return STEREO_DOWNMIX != StereoDownmix::None;
    }

    // some games hardcode a multichannel ASIO output and bail before create_buffers if
    // get_channels reports fewer, so we report at least this many output channels when a
    // stereo extraction is active
    static constexpr long FORCED_OUTPUT_CHANNELS = 8;

    // maps an option string ("front", "center", "rear", "side") to a StereoDownmix value,
    // returning None for anything unrecognized
    static StereoDownmix name_to_stereo_downmix(const char *name);

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

    // quiesces any leftover stream/buffer state before the cached wrapper is handed back
    // for reuse, without destroying the real driver (see wrap_existing)
    void quiesce_for_reuse();

private:
    // create_buffers implementation used when a stereo extraction is active: forwards only
    // the channels the real device has and hands the game throwaway buffers for the rest
    AsioError create_buffers_front_pair(
        AsioBufferInfo *buffer_infos,
        long num_channels,
        long buffer_size,
        AsioCallbacks *callbacks);

    // if any post-processing effect (volume boost or stereo downmix) is active, saves the
    // game's callbacks and returns a proxy callback set (our buffer-switch trampolines) to
    // hand the real driver instead, so we can rework its output buffers after the game
    // fills them. otherwise returns the game's callbacks unchanged. called at create_buffers
    // time, before the stream starts
    AsioCallbacks *install_proxy_callbacks(AsioCallbacks *game_callbacks);

    // records a device output channel whose buffers we scale by the volume boost. queries
    // the real driver for the channel's sample format. called at create_buffers time
    void record_volume_output_channel(const AsioBufferInfo &info);

    // the real device's output sample format, queried from its first output channel. all
    // output channels of a device share one format, so this characterizes them all. returns
    // ASIOSTLastEntry if the device has no output channels or the query fails
    AsioSampleType device_output_sample_type();

    // locates the destination pair (device channels 0/1) and the configured source channels
    // in the game's buffer set so the realtime path can copy the selected pair onto 0/1.
    // a no-op unless STEREO_DOWNMIX selects a non-front pair. called at create_buffers time
    void record_downmix_channels(AsioBufferInfo *buffer_infos, long num_channels, long buffer_size);

    // publishes the captured post-process state to the realtime thread once the buffers
    // exist, making our trampolines start reworking output. called at the end of either
    // create_buffers path
    void publish_post_process(long buffer_size);

    // detaches this instance from the realtime trampolines so they stop touching its
    // buffers. called from dispose_buffers and the destructor
    void detach_post_process();

    // multiplies every recorded output channel's buffer for the given double-buffer index
    // by the volume boost. runs on the driver's realtime thread from our buffer switch
    void apply_output_volume(long double_buffer_index);

    // copies the configured source channel pair onto device channels 0/1 for the given
    // double-buffer index. runs on the driver's realtime thread from our buffer switch
    void apply_downmix(long double_buffer_index);

    // realtime-thread trampolines for the buffer-switch callbacks, handed to the real
    // driver in place of the game's; ASIO callbacks carry no user data, so they reach the
    // active wrapper through active_instance, call the game's original, then rework output.
    // the other two callbacks (sample_rate_did_change, asio_message) are forwarded as the
    // game's own pointers, so they need no trampoline
    static void __cdecl proxy_buffer_switch(long double_buffer_index, AsioBool direct_process);
    static AsioTime * __cdecl proxy_buffer_switch_time_info(
        AsioTime *params, long double_buffer_index, AsioBool direct_process);

    // the single wrapper whose proxy callbacks are installed (ASIO is single-instance with
    // one running stream); read by the static trampolines to reach the right wrapper
    static std::atomic<WrappedAsio *> active_instance;

    IAsio *const pReal;
    const CLSID clsid;

    // registry name of the driver (not get_driver_name), used in our logs as a single
    // unambiguous name; constant for our lifetime
    std::string driver_name;

    // the real driver is initialized exactly once; repeat init() calls are a no-op success
    bool initialized = false;

    // whether the real driver currently has a buffer set / running stream. used to quiesce
    // leftover state when the cached wrapper is reused (see quiesce_for_reuse)
    bool buffers_created = false;
    bool started = false;

    // our own reference count; we hold one reference on pReal and release it when this
    // drops to zero
    std::atomic<ULONG> ref_count {1};

    // throwaway double buffers handed to the channels we discard when a stereo extraction
    // is active (see create_buffers). owned for the lifetime of the buffer set and freed
    // in dispose_buffers; only read by the game from its own bufferSwitch, never by us
    std::vector<std::unique_ptr<uint8_t[]>> dummy_buffers;

    // one device output channel scaled by the volume boost in our buffer switch
    struct VolumeOutputChannel {
        void *buffers[2];
        AsioSampleType type;
    };

    // the game's original callbacks (captured when we install our proxy set) and the proxy
    // set we hand the real driver; the realtime trampolines reach the game's buffer_switch
    // through game_callbacks regardless of which effect is active
    AsioCallbacks game_callbacks {};
    AsioCallbacks proxy_callbacks {};

    // volume boost state, captured at create_buffers time and published to the realtime
    // thread via active_instance once fully built; untouched while the stream runs.
    // volume_active gates whether the realtime path scales any buffers
    bool volume_active = false;
    float volume_gain = 1.0f;
    long volume_buffer_size = 0;
    std::vector<VolumeOutputChannel> volume_channels;

    // one device channel (0 or 1) fed by a source channel during stereo downmix; both
    // buffer pointers are indexed by the ASIO double-buffer index, the same as the channels
    struct DownmixCopy {
        void *dst[2];
        void *src[2];
    };

    // stereo downmix state, captured at create_buffers time and published alongside the
    // volume state; untouched while the stream runs. downmix_active gates whether the
    // realtime path copies the selected source pair onto device channels 0/1. copies[0]
    // feeds device channel 0, copies[1] feeds device channel 1
    bool downmix_active = false;
    DownmixCopy downmix_copies[2] {};
    size_t downmix_bytes = 0;
};
