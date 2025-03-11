#include "core.h"

#include "avs/game.h"
#include "launcher/launcher.h"
#include "misc/wintouchemu.h"
#include "rawinput/rawinput.h"

// static stuff
static int ACIO_WARMUP = 0;
static HHOOK ACIO_KB_HOOK = nullptr;

/*
 * Implementations
 */

// needed for some games to make GetAsyncKeyState() working
static LRESULT CALLBACK ac_io_kb_hook_callback(int nCode, WPARAM wParam, LPARAM lParam) {
    CallNextHookEx(ACIO_KB_HOOK, nCode, wParam, lParam);
    return 0;
}

static char __cdecl ac_io_begin(
        size_t dev,
        const char *ver,
        unsigned int *val,
        size_t flags,
        void *ptr,
        size_t baud)
{
    if (ACIO_KB_HOOK == nullptr) {
        ACIO_KB_HOOK = SetWindowsHookEx(WH_KEYBOARD_LL, ac_io_kb_hook_callback, GetModuleHandle(nullptr), 0);
    }

    // always return success
    if (val && avs::game::is_model("KFC")) {
        *val = 2;
    }

    return 1;
}

static char __cdecl ac_io_begin_get_status() {
    return 1;
}

static int __cdecl ac_io_end(int a1) {
    return 1;
}

static int __cdecl ac_io_end_get_status(int a1) {
    return 1;
}

static void *__cdecl ac_io_get_rs232c_status(char *a1, int a2) {
    return memset(a1, 0, 88);
}

static char __cdecl ac_io_get_version(uint8_t *a1, int a2) {

    // some games have version checks
    // pop'n music only accepts versions bigger than 1.X.X (check yourself), anything starting with 2 works though
    memset(a1 + 5, 2, 1);
    memset(a1 + 6, 0, 1);
    memset(a1 + 7, 0, 1);

    return 1;
}

static const char *__cdecl ac_io_get_version_string() {
    static const char *version = "1.25.0";
    return version;
}

static char __cdecl ac_io_is_active(int a1, int a2) {
    if (a1 == 1 && avs::game::is_model("JMA")) {
        return 1;
    }

    return (char) (++ACIO_WARMUP > 601 ? 1 : 0);
}

static int __cdecl ac_io_is_active2(int a1, int *a2, int a3) {
    ACIO_WARMUP = 601;
    *a2 = 6;
    return 1;
}

static char __cdecl ac_io_is_active_device(int index, int a2) {

    // for scotto
    static bool CHECKED_24 = false;

    // dance evolution
    if (avs::game::is_model("KDM")) {

        // disable mysterious LED devices
        if (index >= 12 && index <= 15)
            return false;
    }

    // scotto
    if (avs::game::is_model("NSC") && index == 24) {

        // scotto expects device index 24 to come online after
        // it initializes device index 22
        if (!CHECKED_24) {
            CHECKED_24 = true;
            return false;
        }

        return true;
    }

    // dunno for what game we did this again
    return (char) (index != 5);
}

static int __cdecl ac_io_reset(int a1) {
    return a1;
}

static int __cdecl ac_io_secplug_set_encodedpasswd(void *a1, int a2) {
    return 1;
}

static int __cdecl ac_io_set_soft_watch_dog(int a1, int a2) {
    return 1;
}

static int __cdecl ac_io_soft_watch_dog_on(int a1) {
    return 1;
}

static int __cdecl ac_io_soft_watch_dog_off() {
    return 1;
}

static int __cdecl ac_io_update(int a1) {

    // flush device output
    RI_MGR->devices_flush_output();

    // update wintouchemu
    wintouchemu::update();

    return 1;
}

static int __cdecl ac_io_get_firmware_update_device_index() {
    return 0xFF;
}

static void __cdecl ac_io_go_firmware_update() {
}

static int __cdecl ac_io_set_get_status_device(int a1) {
    return a1;
}

/*
 * Module stuff
 */

acio::CoreModule::CoreModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("Core", module, hookMode) {
}

void acio::CoreModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_begin);
    ACIO_MODULE_HOOK(ac_io_begin_get_status);
    ACIO_MODULE_HOOK(ac_io_end);
    ACIO_MODULE_HOOK(ac_io_end_get_status);
    ACIO_MODULE_HOOK(ac_io_get_rs232c_status);
    ACIO_MODULE_HOOK(ac_io_get_version);
    ACIO_MODULE_HOOK(ac_io_get_version_string);
    ACIO_MODULE_HOOK(ac_io_is_active);
    ACIO_MODULE_HOOK(ac_io_is_active2);
    ACIO_MODULE_HOOK(ac_io_is_active_device);
    ACIO_MODULE_HOOK(ac_io_reset);
    ACIO_MODULE_HOOK(ac_io_secplug_set_encodedpasswd);
    ACIO_MODULE_HOOK(ac_io_set_soft_watch_dog);
    ACIO_MODULE_HOOK(ac_io_soft_watch_dog_on);
    ACIO_MODULE_HOOK(ac_io_soft_watch_dog_off);
    ACIO_MODULE_HOOK(ac_io_update);
    ACIO_MODULE_HOOK(ac_io_get_firmware_update_device_index);
    ACIO_MODULE_HOOK(ac_io_go_firmware_update);
    ACIO_MODULE_HOOK(ac_io_set_get_status_device);
}
