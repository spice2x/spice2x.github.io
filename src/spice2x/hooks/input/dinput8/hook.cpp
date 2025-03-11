#include "hook.h"

#include <windows.h>
#include <dinput.h>

#include "util/detour.h"
#include "util/logging.h"

#include "fake_backend.h"

//static decltype(DirectInput8Create) *DirectInput8Create_orig = nullptr;

static HRESULT WINAPI DirectInput8Create_hook(
        HINSTANCE hinst,
        DWORD dwVersion,
        REFIID riidltf,
        LPVOID *ppvOut,
        LPUNKNOWN punkOuter)
{
    log_misc("input::dinput8", "DirectInput8Create hook hit");

    if (ppvOut == nullptr) {
        return E_POINTER;
    }

    *ppvOut = new FakeIDirectInput8W();

    return DI_OK;
}

void hooks::input::dinput8::init(HMODULE module) {

    /*
     * This is for the games using DirectInput for keyboard/gamepad controls themselves,
     * for things such as debug controls. We don't want that, neither do we want the game to
     * interfere with our RawInput stuff.
     */

    log_info("input::dinput8", "attaching...");

    // patch IAT
    detour::iat_try("DirectInput8Create", DirectInput8Create_hook, module, "dinput8.dll");

    /*
    if (DirectInput8Create_orig == nullptr) {
        DirectInput8Create_orig = orig;
    }
    */

    log_info("input::dinput8", "attached");
}
