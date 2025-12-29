#include "mme.h"
#include <mmeapi.h>

#include "util/detour.h"
#include "util/logging.h"

namespace hooks::audio::mme {

    static decltype(mixerSetControlDetails) *mixerSetControlDetails_orig = nullptr;

    MMRESULT
    WINAPI
    mixerSetControlDetails_hook(
        HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails) {
        log_misc("audio::mme", "mixerSetControlDetails_hook called; ignoring volume change");
        return MMSYSERR_NOERROR;
    }

    void init(HINSTANCE module) {
        mixerSetControlDetails_orig =
            detour::iat_try("mixerSetControlDetails", mixerSetControlDetails_hook, module);

        if (mixerSetControlDetails_orig != nullptr) {
            log_misc("audio::mme", "mixerSetControlDetails hooked");
        }
    }
}
