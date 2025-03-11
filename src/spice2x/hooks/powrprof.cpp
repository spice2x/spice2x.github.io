#include "powrprof.h"

#include <windows.h>
#include <powrprof.h>

#include "util/detour.h"
#include "util/logging.h"

/*
 * These hooks are required because Sound Voltex Exceed Gear sets `GUID_PROCESSOR_IDLE_DISABLE` to
 * minimize CPU idle downclocking. While this may be good for cab use, it causes unnecessary power
 * usage, battery drain on laptops, and requires a manual `powercfg` command to fix later on, which
 * is annoying.
 */

//static decltype(PowerGetActiveScheme) *PowerGetActiveScheme_orig = nullptr;
//static decltype(PowerSetActiveScheme) *PowerSetActiveScheme_orig = nullptr;
//static decltype(PowerWriteACValueIndex) *PowerWriteACValueIndex_orig = nullptr;

static DWORD WINAPI PowerGetActiveScheme_hook(HKEY UserRootPowerKey, GUID **ActivePolicyGuid) {

    // stubbed
    return ERROR_SUCCESS;
}

static DWORD WINAPI PowerSetActiveScheme_hook(HKEY UserRootPowerKey, const GUID *SchemeGuid) {

    // stubbed
    return ERROR_SUCCESS;
}

static DWORD WINAPI PowerWriteACValueIndex_hook(
        HKEY RootPowerKey,
        const GUID *SchemeGuid,
        const GUID *SubGroupOfPowerSettingsGuid,
        const GUID *PowerSettingGuid,
        DWORD AcValueIndex)
{
    // stubbed
    return ERROR_SUCCESS;
}

void powrprof_hook_init(HMODULE module) {
    log_info("powrprof", "initializing");

    detour::iat_try("PowerGetActiveScheme", &PowerGetActiveScheme_hook, module);
    detour::iat_try("PowerSetActiveScheme", &PowerSetActiveScheme_hook, module);
    detour::iat_try("PowerWriteACValueIndex", &PowerWriteACValueIndex_hook, module);
}
