#include "sleephook.h"

#include "avs/game.h"
#include "util/detour.h"

// settings
static DWORD SLEEPHOOK_MS_MAX;
static DWORD SLEEPHOOK_MS_REPLACE;

static decltype(Sleep) *Sleep_orig = nullptr;
static decltype(SleepEx) *SleepEx_orig = nullptr;

static VOID WINAPI Sleep_hook(DWORD dwMilliseconds) {
    if (dwMilliseconds > SLEEPHOOK_MS_MAX) {
        dwMilliseconds = SLEEPHOOK_MS_REPLACE;
    }

    Sleep_orig(dwMilliseconds);
}

static DWORD WINAPI SleepEx_hook(DWORD dwMilliseconds, BOOL bAltertable) {
    if (dwMilliseconds > SLEEPHOOK_MS_MAX) {
        dwMilliseconds = SLEEPHOOK_MS_REPLACE;
    }

    return SleepEx_orig(dwMilliseconds, bAltertable);
}

void hooks::sleep::init(DWORD ms_max, DWORD ms_replace, HMODULE module) {

    // auto module
    if (!module) {
        module = avs::game::DLL_INSTANCE;
    }

    // settings
    SLEEPHOOK_MS_MAX = ms_max;
    SLEEPHOOK_MS_REPLACE = ms_replace;

    // hook functions
    Sleep_orig = detour::iat_try("Sleep", Sleep_hook, module);
    SleepEx_orig = detour::iat_try("SleepEx", SleepEx_hook, module);
}
