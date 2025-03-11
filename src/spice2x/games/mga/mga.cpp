#include "mga.h"

#include "acio/icca/icca.h"
#include "hooks/devicehook.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/detour.h"
#include "util/sigscan.h"

#include "gunio.h"

namespace games::mga {
    void (__cdecl *ess_eventlog_request_error_orig)(const char *, unsigned int, int, int);

    static int __cdecl log_change_level_hook(int level) {
        log_misc("mga", "ignoring log_change_level({})", level);

        return 1;
    }

    static uintptr_t __cdecl log_change_output_hook(uintptr_t target, uintptr_t a2) {
        log_misc("mga", "ignoring log_change_output(0x{:x}, 0x{:x})", target, a2);

        return 0;
    }

    static bool __cdecl setvolume_hook(uint8_t volume) {
        log_misc("mga", "ignoring setvolume {}", static_cast<int>(volume));
        return false;
    }

    static void __cdecl ess_eventlog_request_error_hook(const char *a1, unsigned int a2, int a3, int a4) {
        log_info("mga", "ess_eventlog_request_error({}, {}, {}, {})", a1, a2, a3, a4);

        //ess_eventlog_request_error_orig(a1, a2, a3, a4);
    }

    MGAGame::MGAGame() : Game("Metal Gear Arcade") {
    }

    void MGAGame::attach() {
        Game::attach();

        // load system.dll (along with the serial functions)
        auto system = libutils::load_library("system.dll");

        // add the gun
        devicehook_init();
        devicehook_add(new SpiceGearGunHandle());

        // fix ICCA
        acio::ICCA_COMPAT_ACTIVE = true;

        // ignore sound engine failure
        if (!replace_pattern(
                system,
                "E8????????84C0750A68F4010000E8????????E8????????E8????????3BF3750433C0EB0C",
                "??????????????????90909090909090909090????????????????????????????????????",
                0, 0))
        {
            log_warning("mga", "failed to patch sound engine");
        }

        // hook setvolume
        if (!detour::iat_try("?setvolume@@YAHPAD@Z", setvolume_hook)) {
            log_warning("mga", "setvolume hook failed");
        }

        // stop the game from redirecting AVS log calls
        detour::iat_try("log_change_level", log_change_level_hook);
        detour::iat_try("log_change_output", log_change_output_hook);

        ess_eventlog_request_error_orig = detour::iat_try("ess_eventlog_request_error", ess_eventlog_request_error_hook);
    }

    void MGAGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}
