#pragma once

#include <utility>
#include <vector>

#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>

struct IAudioClient;
struct IAudioRenderClient;

namespace hooks::audio {

    // Generic WASAPI surround-to-stereo downmix. The real device is opened in stereo while the
    // game keeps writing its native multi-channel audio into a scratch buffer; on release that
    // buffer is mixed down into the two front channels.
    //
    // Callers may pass explicit (left, right) channel pairs to mix. With no pairs, the mix is
    // derived from the format's speaker mask: left/right speakers stay on their own side,
    // center channels fold into both at -3 dB, and LFE is dropped.
    struct Downmix {

        // a source channel routed into one output speaker at the given gain
        struct Contribution {
            int channel;
            float gain;
        };

        // whether the downmix is active for the current stream
        bool enabled = false;

        // size in bytes of one frame of the game's multi-channel format
        int game_frame_size = 0;

        // size in bytes of a single sample (per channel)
        int bytes_per_sample = 0;

        // enable the downmix for the given game format and fill stereo_out with the equivalent
        // stereo format to open the real device with. `pairs` picks which channels to mix; an
        // empty list mixes by speaker layout (see class comment).
        void setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *stereo_out,
                   const std::vector<std::pair<int, int>> &pairs = {});

        // build the stereo format equivalent to game_format (same sample rate and bit depth).
        static void make_stereo_format(const WAVEFORMATEX *game_format,
                                       WAVEFORMATEXTENSIBLE *stereo_out);

        // initialize the real device with the stereo format. downmixing reduces the channel count,
        // shrinking the buffer's byte size, so the duration the game sized for its multi-channel
        // format can leave the smaller stereo buffer unaligned. on AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED
        // this performs the standard WASAPI realignment and retries.
        HRESULT initialize(IAudioClient *real, AUDCLNT_SHAREMODE share_mode, DWORD stream_flags,
                           REFERENCE_TIME buffer_duration, REFERENCE_TIME periodicity,
                           const WAVEFORMATEX *device_format, LPCGUID session_guid);

        // mix `frames` frames of multi-channel `src` down into stereo `dst`.
        void process(BYTE *dst, const BYTE *src, UINT32 frames) const;

        // grab the real stereo device buffer and hand the game the scratch buffer to write into.
        HRESULT get_buffer(IAudioRenderClient *real, UINT32 frames, BYTE **ppData);

        // mix the scratch buffer into the stereo device buffer held since get_buffer. the caller
        // owns releasing the device buffer afterwards (see current_buffer / buffer_released).
        void write_device_buffer(UINT32 frames, DWORD flags);

        // the real device buffer currently held, or null.
        BYTE *current_buffer() const { return this->device_buffer; }

        // forget the held device buffer once the caller has released it.
        void buffer_released() { this->device_buffer = nullptr; }

    private:

        // build the mix from the source speaker layout (used when no pairs are given)
        void build_layout_mix(const WAVEFORMATEX *game_format);

        // source channels summed into each output speaker
        std::vector<Contribution> left_mix;
        std::vector<Contribution> right_mix;

        // buffer the game writes its multi-channel audio into between get/release
        std::vector<BYTE> scratch;

        // the real stereo device buffer currently held, or null
        BYTE *device_buffer = nullptr;

        // leading buffers to silence to avoid a pop on stream start
        int buffers_to_mute = 16;
    };
}
