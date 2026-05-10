#include "rb.h"

#include "avs/game.h"
#include "hooks/devicehook.h"
#include "hooks/audio/backends/dsound/dsound_backend.h"
#include "util/detour.h"
#include "util/precise_timer.h"
#include "util/logging.h"

#include "touch.h"

static decltype(SleepEx) *SleepEx_orig;

static DWORD WINAPI SleepEx_hook(DWORD dwMilliseconds, BOOL bAltertable) {

    // increase touch poll from ~110 FPS to ~500 FPS (Sleep) or ~1000 FPS (Win10 high-res timer)
    if (dwMilliseconds == 8) {
        dwMilliseconds = 1;
    }

    // most calls from rb are actually non-alertable
    if (!bAltertable) {
        static thread_local timeutils::PreciseSleepTimer timer;
        timer.sleep(dwMilliseconds);
        return 0;
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
