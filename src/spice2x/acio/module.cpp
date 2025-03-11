#include "module.h"
#include "util/logging.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "avs/game.h"

const char *acio::hook_mode_str(acio::HookMode hook_mode) {
    switch (hook_mode) {
        case HookMode::INLINE:
            return "Inline";
        case HookMode::IAT:
            return "IAT";
        default:
            return "Unknown";
    }
}

/*
 * Hook functions depending on the specified mode.
 * We don't care about errors here since different versions of libacio contain different feature sets,
 * which means that not all hooks must/can succeed.
 */
void acio::ACIOModule::hook(void *func, const char *func_name) {
    switch (this->hook_mode) {
        case HookMode::INLINE:
            detour::inline_hook(func, libutils::try_proc(this->module, func_name));
            break;
        case HookMode::IAT:
            detour::iat_try(func_name, func);
            break;
        default:
            log_warning("acio", "unable to hook using mode {}", hook_mode_str(this->hook_mode));
    }
}

void acio::ACIOModule::attach() {
    log_info("acio", "module attach: {} {}", this->name, hook_mode_str(this->hook_mode));
    this->attached = true;
}
