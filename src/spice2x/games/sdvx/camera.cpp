
// set version to Windows 7 to enable Media Foundation functions
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601

// turn on C-style COM objects
#define CINTERFACE

#include "camera.h"

#include <mfobjects.h>
#include <mfidl.h>

#include "avs/game.h"
#include "util/detour.h"
#include "util/utils.h"

namespace games::sdvx {

    static HRESULT WINAPI MFEnumDeviceSources_hook(IMFAttributes *pAttributes, IMFActivate ***pppSourceActivate,
            UINT32 *pcSourceActivate) {

        *pppSourceActivate = nullptr;
        *pcSourceActivate = 0;
        log_misc("sdvx", "MFEnumDeviceSources_hook called, returning 0 cameras");
        return S_OK;
    }

    void camera_init() {

        // camera media framework hook
        log_info("sdvx", "installing camera hooks...");
        detour::iat_try("MFEnumDeviceSources", MFEnumDeviceSources_hook, avs::game::DLL_INSTANCE);
    }
}
