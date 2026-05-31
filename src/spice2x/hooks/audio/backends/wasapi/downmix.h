#pragma once

#include <optional>
#include <vector>

#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>

#include "hooks/audio/audio.h"

struct IAudioClient;
struct IAudioRenderClient;

namespace hooks::audio {

    // Generic WASAPI surround-to-stereo downmix. The real device is opened in stereo while the
    // game keeps writing its native multi-channel audio into a scratch buffer; on release that
    // buffer is mixed down into the two front channels.
    //
    // The mix is derived from the source format's speaker mask according to the selected
    // DownmixAlgorithm:
    //   FrontOnly / RearOnly / SideOnly - keep only that group of channels, routed to their side
    //   AC4       - AC-4 stereo downmix coefficients (ETSI TS 103 190-1 §6.2.17): front left/right
    //               pass at 0 dB, center and surrounds fold in at -3 dB, LFE dropped
    //   Normalize - every channel folded in (center to both sides) with each output side averaged
    //               so its channels are equally loud, LFE dropped
    struct Downmix {

        // a source channel routed into one output speaker at the given gain
        struct Contribution {
            int channel;
            float gain;
        };

        // map an option value (front/rear/side/ac4/normalize) to its algorithm.
        static std::optional<DownmixAlgorithm> name_to_algorithm(const char *value) {
            if (_stricmp(value, "front") == 0) {
                return DownmixAlgorithm::FrontOnly;
            } else if (_stricmp(value, "rear") == 0) {
                return DownmixAlgorithm::RearOnly;
            } else if (_stricmp(value, "side") == 0) {
                return DownmixAlgorithm::SideOnly;
            } else if (_stricmp(value, "ac4") == 0) {
                return DownmixAlgorithm::AC4;
            } else if (_stricmp(value, "normalize") == 0) {
                return DownmixAlgorithm::Normalize;
            }

            return std::nullopt;
        }

        // whether the downmix is active for the current stream
        bool enabled = false;

        // algorithm used to fold the multi-channel audio into stereo
        DownmixAlgorithm algorithm = DownmixAlgorithm::AC4;

        // size in bytes of one frame of the game's multi-channel format
        int game_frame_size = 0;

        // size in bytes of a single sample (per channel)
        int bytes_per_sample = 0;

        // whether samples are IEEE floating point rather than integer PCM
        bool is_float = false;

        // enable the downmix for the given game format and fill stereo_out with the equivalent
        // stereo format to open the real device with.
        void setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *stereo_out,
                   DownmixAlgorithm algorithm);

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

        // build the mix from the source speaker layout for the selected algorithm
        void build_layout_mix(const WAVEFORMATEX *game_format);

        // per-algorithm builders, each filling left_mix / right_mix from the speaker mask
        void build_ac4_mix(DWORD mask, int channels);
        void build_extract_mix(DWORD mask, int channels, DWORD keep);
        void build_normalize_mix(DWORD mask, int channels);

        // fallback for streams without a speaker mask: fold interleaved L/R pairs at `gain`
        void build_pairs_mix(int channels, float gain);

        // append one source channel to the output side(s) matching its speaker, at `gain`
        void add_channel(int channel, DWORD speaker, float gain);

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
