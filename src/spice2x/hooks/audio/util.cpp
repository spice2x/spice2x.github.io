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
            return fmt::format("ShareMode(0x{:08x})", share_mode);
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
    log_info("audio", "Wave Format:");

    // format specific
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto format = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat);
        log_info("audio", "... SubFormat           : {}", guid2s(format->SubFormat));
    } else {
        log_info("audio", "... wFormatTag          : {}", pFormat->wFormatTag);
    }

    // generic
    log_info("audio", "... nChannels           : {}", pFormat->nChannels);
    log_info("audio", "... nSamplesPerSec      : {}", pFormat->nSamplesPerSec);
    log_info("audio", "... nAvgBytesPerSec     : {}", pFormat->nAvgBytesPerSec);
    log_info("audio", "... nBlockAlign         : {}", pFormat->nBlockAlign);
    log_info("audio", "... wBitsPerSample      : {}", pFormat->wBitsPerSample);

    // format specific
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto format = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat);

        if (pFormat->wBitsPerSample == 0) {
            log_info("audio", "... wSamplesPerBlock    : {}", format->Samples.wSamplesPerBlock);
        } else {
            log_info("audio", "... wValidBitsPerSample : {}", format->Samples.wValidBitsPerSample);
        }

        log_info("audio", "... dwChannelMask       : {}", channel_mask_str(format->dwChannelMask));
    }
}
