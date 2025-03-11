#include "avs/game.h"
#include "hpm.h"

#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

#include <mmsystem.h>

namespace games::hpm {

    BOOL WINAPI SetCurrentDirectoryW_hook(LPCTSTR lpPathName) {
        return true;
    }

    static decltype(mixerSetControlDetails)* mixerSetControlDetails_real = nullptr;

    static MMRESULT WINAPI mixerSetControlDetails_hook(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails) {

        mixerSetControlDetails_real(hmxobj, pmxcd, fdwDetails);
        return MMSYSERR_NOERROR;
    }

    HPMGame::HPMGame() : Game("HELLO! Pop'n Music") {
    }

    void HPMGame::attach() {
        Game::attach();

        HMODULE kernel32_module = libutils::try_module("kernel32.dll");

        // patches
        detour::inline_hook((void *) SetCurrentDirectoryW_hook, libutils::try_proc(
                kernel32_module, "SetCurrentDirectoryW"));

        mixerSetControlDetails_real = detour::iat_try("mixerSetControlDetails", mixerSetControlDetails_hook, avs::game::DLL_INSTANCE);
    }
}
