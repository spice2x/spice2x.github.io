#include "rb.h"

#include "avs/game.h"
#include "hooks/devicehook.h"
#include "hooks/audio/backends/dsound/dsound_backend.h"
#include "util/detour.h"

#include "touch.h"

static decltype(SleepEx) *SleepEx_orig;

static DWORD WINAPI SleepEx_hook(DWORD dwMilliseconds, BOOL bAltertable) {

    /*
     * Increase touch poll from ~110 FPS to ~500 FPS
     */
    if (dwMilliseconds == 8) {
        static bool initialized = false;
        if (!initialized) {
            initialized = true;

            // if we only sleep for 1ms we also don't need the high priority RB sets
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        }
        dwMilliseconds = 1;
    }

    // call original
    return SleepEx_orig(dwMilliseconds, bAltertable);
}

games::rb::RBGame::RBGame() : Game("Reflec Beat") {
}

void games::rb::RBGame::attach() {
    Game::attach();

    // init stuff
    devicehook_init();

    // add touch device
    devicehook_add(new ReflecBeatTouchDeviceHandle(false));

    if (avs::game::is_model({"KBR", "LBR"})) {
        // dsound.dll hook
        audio_dsound_init();
    }

    // hooks
    SleepEx_orig = detour::iat_try("SleepEx", SleepEx_hook, avs::game::DLL_INSTANCE);
}

void games::rb::RBGame::detach() {
    Game::detach();
    devicehook_dispose();
}
