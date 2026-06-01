#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>

#include "hooks/audio/audio.h"

struct IAudioClient;
struct IAudioRenderClient;

namespace hooks::audio {

    // Streaming sample-rate converter for the WASAPI render path. The real device is opened at the
    // target rate while the game keeps writing its native-rate audio into a scratch buffer; on
    // release that buffer is converted with a windowed-sinc kernel and pushed to the device.
    // Channel count and sample format are preserved; only the sample rate changes.
    //
    // Frame counts differ between the two rates, so unlike the per-frame downmix this is stateful:
    // a fractional read position and a window of input history carry across ReleaseBuffer calls,
    // and the device buffer is only filled up to the space the device currently has free.
    struct Resampler {

        // whether the resampler is active for the current stream
        bool enabled = false;

        // decide whether the stream should be resampled and to which rate. returns the target rate
        // when RESAMPLE_RATE is set and differs from the game's rate, otherwise nullopt.
        static std::optional<uint32_t> resolve(const WAVEFORMATEX *game_format);

        // enable resampling for game_format and fill device_out with the equivalent format at the
        // target rate to open the real device with.
        void setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *device_out,
                   uint32_t target_rate);

        // build the device format equivalent to game_format at target_rate (same channels/depth).
        static void make_device_format(const WAVEFORMATEX *game_format,
                                       WAVEFORMATEXTENSIBLE *device_out, uint32_t target_rate);

        // initialize the real device at the target rate, performing the standard WASAPI buffer
        // realignment retry on AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED.
        HRESULT initialize(IAudioClient *real, AUDCLNT_SHAREMODE share_mode, DWORD stream_flags,
                           REFERENCE_TIME buffer_duration, REFERENCE_TIME periodicity,
                           const WAVEFORMATEX *device_format, LPCGUID session_guid);

        // translate a device-rate frame count to the equivalent game-rate count, so the buffer-size
        // and padding values reported to the game stay paced at the game's native rate.
        UINT32 frames_device_to_game(UINT32 device_frames) const;
        UINT32 padding_device_to_game(UINT32 device_padding) const;

        // hand the game a scratch buffer sized for `frames` of its native format to write into.
        HRESULT get_buffer(UINT32 frames, BYTE **ppData);

        // pointer to the input scratch (sized by get_buffer). when chained after the downmix, the
        // downmix writes its stereo output here for the resampler to consume on the next flush.
        BYTE *input_data() { return this->scratch.data(); }

        // convert the `frames` the game wrote and push exactly one full device buffer of output
        // to the real render client. `boost` is applied to the converted output. only event-driven
        // exclusive streams are supported.
        HRESULT flush(IAudioRenderClient *real, IAudioClient *client, UINT32 frames, DWORD flags,
                      float boost);

    private:

        // append `frames` of the scratch buffer (native format), or silence, to the input queue
        void enqueue_input(UINT32 frames, bool silent);

        // produce exactly out_frames output frames from all input currently available, stretching
        // the block with a per-call step so the input maps onto the full buffer. event-driven
        // exclusive streams must fill the whole device buffer every period.
        UINT32 produce_exact(UINT32 out_frames);

        // convolve the windowed-sinc kernel at the current in_pos and append the resulting frame
        // (one sample per channel) to out_float
        void emit_frame();

        // drop input frames that in_pos has advanced past, keeping a window of history for the
        // next block's left context
        void drop_consumed();

        // convert the first `frames` of out_float to the device format, scaled by `gain`
        void write_output(BYTE *dst, UINT32 frames, float gain) const;

        // sample format of the stream
        int channels = 0;
        int bytes_per_sample = 0;
        bool is_float = false;
        int game_frame_size = 0;

        uint32_t src_rate = 0;
        uint32_t dst_rate = 0;

        // sinc low-pass cutoff (1.0 when upsampling, dst/src when downsampling) and window radius
        double cutoff = 1.0;
        int half_taps = 16;

        // interleaved float input queue and the fractional read position within it (in frames)
        std::vector<float> in_queue;
        double in_pos = 0.0;

        // interleaved float scratch for produced output
        std::vector<float> out_float;

        // buffer the game writes its native-rate audio into between get_buffer / flush
        std::vector<BYTE> scratch;

        // cached device buffer size (frames); a full buffer is produced every period
        UINT32 device_buffer_frames = 0;

        // leading buffers to silence to avoid a pop on stream start
        int buffers_to_mute = 16;
    };
}
