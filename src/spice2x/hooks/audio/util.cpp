#include "util.h"

#include <ks.h>
#include <ksmedia.h>

#include "util/flags_helper.h"
#include "util/logging.h"
#include "util/utils.h"

std::string channel_mask_str(DWORD channel_mask) {
    FLAGS_START(channel_mask);
    FLAG(channel_mask, SPEAKER_FRONT_LEFT);
    FLAG(channel_mask, SPEAKER_FRONT_RIGHT);
    FLAG(channel_mask, SPEAKER_FRONT_CENTER);
    FLAG(channel_mask, SPEAKER_LOW_FREQUENCY);
    FLAG(channel_mask, SPEAKER_BACK_LEFT);
    FLAG(channel_mask, SPEAKER_BACK_RIGHT);
    FLAG(channel_mask, SPEAKER_FRONT_LEFT_OF_CENTER);
    FLAG(channel_mask, SPEAKER_FRONT_RIGHT_OF_CENTER);
    FLAG(channel_mask, SPEAKER_BACK_CENTER);
    FLAG(channel_mask, SPEAKER_SIDE_LEFT);
    FLAG(channel_mask, SPEAKER_SIDE_RIGHT);
    FLAG(channel_mask, SPEAKER_TOP_CENTER);
    FLAG(channel_mask, SPEAKER_TOP_FRONT_LEFT);
    FLAG(channel_mask, SPEAKER_TOP_FRONT_CENTER);
    FLAG(channel_mask, SPEAKER_TOP_FRONT_RIGHT);
    FLAG(channel_mask, SPEAKER_TOP_BACK_LEFT);
    FLAG(channel_mask, SPEAKER_TOP_BACK_CENTER);
    FLAG(channel_mask, SPEAKER_TOP_BACK_RIGHT);
    FLAGS_END(channel_mask);
}

std::string share_mode_str(AUDCLNT_SHAREMODE share_mode) {
    switch (share_mode) {
        ENUM_VARIANT(AUDCLNT_SHAREMODE_SHARED);
        ENUM_VARIANT(AUDCLNT_SHAREMODE_EXCLUSIVE);
        default:
            return fmt::format("ShareMode(0x{:08x})", static_cast<uint32_t>(share_mode));
    }
}

void copy_wave_format(WAVEFORMATEXTENSIBLE *destination, const WAVEFORMATEX *source) {
    if (source->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        memcpy(destination, source, sizeof(WAVEFORMATEXTENSIBLE));
    } else {
        memcpy(destination, source, sizeof(WAVEFORMATEX));
    }
}

void print_format(const WAVEFORMATEX *pFormat) {
    log_info("audio::wasapi", "Wave Format:");

    // format specific
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto format = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat);
        log_info("audio::wasapi", "... SubFormat           : {}", guid2s(format->SubFormat));
    } else {
        log_info("audio::wasapi", "... wFormatTag          : {}", pFormat->wFormatTag);
    }

    // generic
    log_info("audio::wasapi", "... nChannels           : {}", pFormat->nChannels);
    log_info("audio::wasapi", "... nSamplesPerSec      : {}", pFormat->nSamplesPerSec);
    log_info("audio::wasapi", "... nAvgBytesPerSec     : {}", pFormat->nAvgBytesPerSec);
    log_info("audio::wasapi", "... nBlockAlign         : {}", pFormat->nBlockAlign);
    log_info("audio::wasapi", "... wBitsPerSample      : {}", pFormat->wBitsPerSample);

    // format specific
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto format = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat);

        if (pFormat->wBitsPerSample == 0) {
            log_info("audio::wasapi", "... wSamplesPerBlock    : {}", format->Samples.wSamplesPerBlock);
        } else {
            log_info("audio::wasapi", "... wValidBitsPerSample : {}", format->Samples.wValidBitsPerSample);
        }

        log_info("audio::wasapi", "... dwChannelMask       : {}", channel_mask_str(format->dwChannelMask));
    }
}
