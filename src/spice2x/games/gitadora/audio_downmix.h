#pragma once

#include <vector>

#include <windows.h>
#include <mmreg.h>

struct IAudioRenderClient;

namespace games::gitadora {

    // GITADORA arena cabinets request a 7.1 (8-channel) surround audio format. On systems
    // that cannot open such a format (e.g. stereo-only endpoints), or when the user explicitly
    // wants stereo output, we transparently open the real device as stereo and downmix the
    // game's 7.1 output ourselves.
    //
    // This struct holds the per-stream state required to perform that downmix. The game keeps
    // writing its native 7.1 frames into a scratch buffer while we sum the 7.1 front and rear
    // channels into the stereo front speakers on release.
    struct ArenaDownmix {

        // whether the downmix is active for the current stream
        bool enabled = false;

        // size in bytes of one frame of the game's 7.1 format
        int game_frame_size = 0;

        // size in bytes of a single sample (per channel)
        int bytes_per_sample = 0;

        // true when the given format should be downmixed: arena model, two-channel option
        // enabled, and a 7.1 extensible format was requested.
        static bool is_downmix_required(const WAVEFORMATEX *format);

        // fix up the legacy 7.1 channel mask used by the game to the modern surround layout,
        // making it more compatible with modern audio cards. Only used when not downmixing.
        static void fix_legacy_mask(WAVEFORMATEX *format);

        // configure the downmix from the game's requested format and fill stereo_out with the
        // equivalent stereo device format that should be passed to the real device.
        void setup(const WAVEFORMATEX *game_format, WAVEFORMATEXTENSIBLE *stereo_out);

        // downmix `frames` frames from the 7.1 `src` buffer into the stereo `dst` buffer by
        // summing the front and rear channels into the front speakers.
        void process(BYTE *dst, const BYTE *src, UINT32 frames) const;

        // reserve the real (stereo) device buffer from `real` and hand the game the
        // 7.1 scratch buffer (via ppData) to write its native format into.
        HRESULT get_buffer(IAudioRenderClient *real, UINT32 frames, BYTE **ppData);

        // downmix the scratch buffer into the reserved stereo device buffer and release it.
        HRESULT release_buffer(IAudioRenderClient *real, UINT32 frames, DWORD flags);

    private:

        // scratch buffer the game writes its multi-channel audio into between get/release
        std::vector<BYTE> scratch;

        // currently reserved stereo device buffer, or null when none is held
        BYTE *device_buffer = nullptr;

        // number of leading buffers to silence to avoid an audio pop on stream start
        int buffers_to_mute = 16;
    };
}
