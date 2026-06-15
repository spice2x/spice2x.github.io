#pragma once

#include <cstdint>
#include <vector>

#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>

struct IAudioRenderClient;

namespace hooks::audio {

    // The -wasapishared option redirects an exclusive WASAPI stream to shared mode, so other apps
    // can play sound and devices that can't open the exclusive format still work, at the cost of
    // some latency. Only PCM / float is converted; bitstream (AC-3 / DTS) is left alone.
    struct SharedRedirect {

        // true once apply() has redirected an exclusive request. gates the buffer clamp; stays false
        // for a natively-shared stream (it paces itself, so must not be clamped).
        bool redirected_from_exclusive = false;

        // whether an exclusive-mode request should be redirected, given the -wasapishared option.
        // only PCM / float is eligible; bitstream (AC-3 / DTS) is left in exclusive mode.
        static bool wants(AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX *format);

        // redirect an exclusive request to shared mode. caller must have checked wants() first.
        void apply(AUDCLNT_SHAREMODE *share_mode, DWORD *stream_flags, REFERENCE_TIME *periodicity);

        // clamp a reported buffer size to one device period. the FIFO bridge below is what prevents
        // the overflow; this just keeps the game's per-event writes small so the bridge adds minimal
        // latency. caches the chosen value for virtual_padding. a no-op unless redirected.
        UINT32 clamp_buffer_size(IAudioClient *real, uint32_t sample_rate, UINT32 device_frames);

        // FIFO bridge: the redirected game writes a whole reported buffer per event paced by its own
        // callback, not the shared engine clock, so a full-buffer write can intermittently exceed the
        // double-buffered shared free space (AUDCLNT_E_BUFFER_TOO_LARGE). The game instead writes
        // directly into a FIFO that is drained to the device only as fast as it frees space - the
        // same free-space-clamped approach the timer-driven resampler uses.

        // arm the bridge once the redirected stream is initialized. frame_bytes is one frame's size
        // in the game's (== device, via AUTOCONVERTPCM) format.
        void enable_bridge(int frame_bytes);

        // whether the FIFO bridge is active (a redirect was applied and armed).
        bool bridge_enabled() const { return this->frame_bytes > 0; }

        // reserve `frames` at the FIFO tail and hand the game a pointer into it to write in place.
        // must be paired with commit_write, which trims the reservation to the frames written.
        BYTE *begin_write(UINT32 frames);

        // trim the reservation from begin_write to the `frames` actually written (zeroing if silent).
        void commit_write(UINT32 frames, bool silent);

        // padding to report to a game that polls GetCurrentPadding while the bridge is active: the
        // FIFO fill level, capped to the reported buffer size so the game's free-space calculation
        // (reported_buffer - padding) reflects room in the virtual buffer rather than the device's.
        UINT32 virtual_padding() const;

        // push as many queued frames as the real device has free, applying `boost`, keeping the rest
        // for the next call. `real` is the wrapped render client's underlying interface; `client` is
        // the underlying audio client used to query the device's free space.
        HRESULT drain(IAudioRenderClient *real, IAudioClient *client,
                const WAVEFORMATEXTENSIBLE &device_format, float boost);

    private:

        // frames currently queued in the FIFO and not yet handed to the device.
        UINT32 pending_frames() const;

        // FIFO bridge state (see enable_bridge). fifo holds audio queued for the device in the
        // game's interleaved frame format; the game writes new frames directly into its tail between
        // begin_write and commit_write. frame_bytes > 0 doubles as the "bridge armed" flag (see
        // bridge_enabled). pending_write_offset marks the tail reservation handed to begin_write.
        int frame_bytes = 0;
        UINT32 device_buffer_frames = 0;
        UINT32 reported_frames = 0;
        int buffers_to_mute = 4;
        size_t pending_write_offset = 0;
        std::vector<BYTE> fifo;
    };
}

