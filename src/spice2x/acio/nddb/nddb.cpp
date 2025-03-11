#include "nddb.h"

#include "avs/game.h"
#include "misc/eamuse.h"
#include "util/utils.h"

// static stuff
static uint8_t STATUS_BUFFER[4] {};
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static void __cdecl ac_io_nddb_control_pwm(int a1, int a2) {
    log_misc("acio::nddb", "ac_io_nddb_control_pwm({}, {})", a1, a2);
}

static void __cdecl ac_io_nddb_control_solenoide(int a1, int a2) {
    log_misc("acio::nddb", "ac_io_nddb_control_solenoide({}, {})", a1, a2);
}

static bool __cdecl ac_io_nddb_create_get_status_thread() {
    return true;
}

static bool __cdecl ac_io_nddb_destroy_get_status_thread() {
    return true;
}

static void __cdecl ac_io_nddb_get_control_status_buffer(void *buffer) {
}

static bool __cdecl ac_io_nddb_req_solenoide_control(uint8_t *buffer) {
    log_misc("acio::nddb", "ac_io_nddb_req_solenoide_control");

    return true;
}

static bool __cdecl ac_io_nddb_update_control_status_buffer() {
    return true;
}

/*
 * Module stuff
 */

acio::NDDBModule::NDDBModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("NDDB", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::NDDBModule::attach() {
    ACIOModule::attach();

    ACIO_MODULE_HOOK(ac_io_nddb_control_pwm);
    ACIO_MODULE_HOOK(ac_io_nddb_control_solenoide);
    ACIO_MODULE_HOOK(ac_io_nddb_create_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_nddb_destroy_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_nddb_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_nddb_req_solenoide_control);
    ACIO_MODULE_HOOK(ac_io_nddb_update_control_status_buffer);
}
