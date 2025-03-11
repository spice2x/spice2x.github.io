#pragma once

#include "asiosys.h"
#include "asio.h"

#include <windows.h>
#include <combaseapi.h>

interface IAsio : public IUnknown {
    virtual AsioBool __thiscall init(void *sys_handle) = 0;
    virtual void __thiscall get_driver_name(char *name) = 0;
    virtual long __thiscall get_driver_version() = 0;
    virtual void __thiscall get_error_message(char *string) = 0;
    virtual AsioError __thiscall start() = 0;
    virtual AsioError __thiscall stop() = 0;
    virtual AsioError __thiscall get_channels(long *num_input_channels, long *num_output_channels) = 0;
    virtual AsioError __thiscall get_latencies(long *input_latency, long *output_latency) = 0;
    virtual AsioError __thiscall get_buffer_size(
        long *min_size,
        long *max_size,
        long *preferred_size,
        long *granularity) = 0;
    virtual AsioError __thiscall can_sample_rate(AsioSampleRate sample_rate) = 0;
    virtual AsioError __thiscall get_sample_rate(AsioSampleRate *sample_rate) = 0;
    virtual AsioError __thiscall set_sample_rate(AsioSampleRate sample_rate) = 0;
    virtual AsioError __thiscall get_clock_sources(ASIOClockSource *clocks, long *num_sources) = 0;
    virtual AsioError __thiscall set_clock_source(long reference) = 0;
    virtual AsioError __thiscall get_sample_position(ASIOSamples *s_pos, ASIOTimeStamp *t_stamp) = 0;
    virtual AsioError __thiscall get_channel_info(AsioChannelInfo *info) = 0;
    virtual AsioError __thiscall create_buffers(
        AsioBufferInfo *buffer_infos,
        long num_channels,
        long buffer_size,
        AsioCallbacks *callbacks) = 0;
    virtual AsioError __thiscall dispose_buffers() = 0;
    virtual AsioError __thiscall control_panel() = 0;
    virtual AsioError __thiscall future(long selector, void *opt) = 0;
    virtual AsioError __thiscall output_ready() = 0;
};
