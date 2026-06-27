#include "sdvx_live2d.h"

// only the Live2D-capable SDVX versions are 64-bit, so the whole feature is
// compiled out of 32-bit builds.
#ifdef SPICE64

#include <string>

#include "hooks/graphics/graphics.h"
#include "launcher/logger.h"
#include "util/logging.h"

namespace games::sdvx {

    // Live2D in-game scene detection (for the -sdvxnolive2d "ingame" option).
    //
    // the game logs scene transitions as "I:Attach: in <SCENE>" / "I:Detach: in
    // <SCENE>". several scenes correspond to in-song gameplay (with the heavy
    // Live2D Cubism rendering); we watch those log lines and keep the shared flag
    // the d3d9 backend reads up to date. the hook never alters the log output
    // (always returns false).
    static bool live2d_scene_log_hook(
            void *user, const std::string &data, logger::Style style, std::string &out) {
        // any of these scenes counts as in-song gameplay (different play modes)
        static const char *const gameplay_scenes[] = {
            "in ALTERNATIVE_GAME_SCENE",
            "in MEGAMIX_GAME_SCENE",
            "in MEGAMIX_BATTLE",
            "in BATTLE_GAME_SCENE",
            "in AUTOMATION_GAME_SCENE",
            "in ARENA_GAME_SCENE",
        };
        bool in_gameplay_scene = false;
        for (const auto *scene : gameplay_scenes) {
            if (data.find(scene) != std::string::npos) {
                in_gameplay_scene = true;
                break;
            }
        }
        if (!in_gameplay_scene) {
            return false;
        }
        // note: log messages here must NOT contain any matched scene token, else
        // this hook would re-enter itself when the message is pushed.
        if (data.find("I:Attach: in ") != std::string::npos) {
            if (!GRAPHICS_SDVX_LIVE2D_IN_GAMEPLAY.exchange(true, std::memory_order_relaxed)) {
                log_info("sdvx", "Live2D skip: entering gameplay");
            }
        } else if (data.find("I:Detach: in ") != std::string::npos) {
            if (GRAPHICS_SDVX_LIVE2D_IN_GAMEPLAY.exchange(false, std::memory_order_relaxed)) {
                log_info("sdvx", "Live2D skip: leaving gameplay");
            }
        }
        return false;
    }

    void live2d_scene_detection_init() {
        static bool installed = false;
        if (installed) {
            return;
        }
        installed = true;
        // the logger's hook list is a persistent static, so registering here is
        // safe even though this runs before logger::start(). we intentionally do
        // NOT log a confirmation now: at this point the log file isn't open yet
        // and the message would be dropped. the entering/leaving-gameplay lines
        // above provide runtime confirmation once the logger is running.
        logger::hook_add(live2d_scene_log_hook, nullptr);
    }
}

#endif // SPICE64
