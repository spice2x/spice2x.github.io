#include "winuser.h"

#include "util/detour.h"
#include "util/logging.h"

// hooks to prevent display scaling changes (e.g., SDVX)

static decltype(DisplayConfigSetDeviceInfo) *DisplayConfigSetDeviceInfo_real = nullptr;

static LONG WINAPI DisplayConfigSetDeviceInfo_hook(DISPLAYCONFIG_DEVICE_INFO_HEADER *setPacket) {
    // seems to be using some undocumented API to change the monitor scaling (type == -4)
    if (setPacket && setPacket->type == 0xFFFFFFFC) {
        log_misc("winuser", "DisplayConfigSetDeviceInfo_hook: ignoring request to change monitor scaling");
        return ERROR_SUCCESS;
    }
    return DisplayConfigSetDeviceInfo_real(setPacket);
}

void winuser_hook_init(HMODULE module) {
    log_info("winuser", "initializing");
    DisplayConfigSetDeviceInfo_real =
        detour::iat_try("DisplayConfigSetDeviceInfo", DisplayConfigSetDeviceInfo_hook, module);
}
