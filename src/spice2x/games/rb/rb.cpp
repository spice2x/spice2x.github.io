#include "rb.h"

#include "avs/game.h"
#include "hooks/devicehook.h"
#include "hooks/audio/backends/dsound/dsound_backend.h"
#include "util/detour.h"
#include "util/precise_timer.h"
#include "util/logging.h"
#include "cfg/configurator.h"

#include "touch.h"

namespace games::rb {
    uint16_t TOUCH_POLL_RATE = 0;
}

static decltype(SleepEx) *SleepEx_orig;

static DWORD WINAPI SleepEx_hook(DWORD dwMilliseconds, BOOL bAlertable) {

    // increase touch poll rate
    if (dwMilliseconds == 8) {
        dwMilliseconds = 1000 / games::rb::TOUCH_POLL_RATE;
    }

    // most calls from rb are actually non-alertable
    if (!bAlertable) {
        static thread_local timeutils::PreciseSleepTimer timer;
        timer.sleep(dwMilliseconds);
        return 0;
    }

    // call original
    return SleepEx_orig(dwMilliseconds, bAlertable);
}

games::rb::RBGame::RBGame() : Game("Reflec Beat") {
}

void games::rb::RBGame::attach() {
    Game::attach();

    if (1000 < games::rb::TOUCH_POLL_RATE) {
        log_fatal("rb", "-rbtouchhz is set too high; cannot exceed 1000");
    }

    // init stuff
    devicehook_init();

    // add touch device
    devicehook_add(new ReflecBeatTouchDeviceHandle(false));

    if (avs::game::is_model({"KBR", "LBR"})) {
        // dsound.dll hook
        audio_dsound_init();
    }

    // hooks
    if (0 < TOUCH_POLL_RATE) {
        log_info("rb", "force increasing touch poll rate by hooking SleepEx ({}Hz)", TOUCH_POLL_RATE);
        SleepEx_orig = detour::iat_try("SleepEx", SleepEx_hook, avs::game::DLL_INSTANCE);
    } else {
        log_info("rb", "not changing touch poll rate (~125Hz by default)");
    }
}

void games::rb::RBGame::detach() {
    Game::detach();
    devicehook_dispose();
}
