#include "i36i.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"
#include "misc/eamuse.h"
#include "avs/game.h"

//using namespace GameAPI;

// static stuff
static uint8_t STATUS_BUFFER[48];
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static bool __cdecl ac_io_i36i_ps3_controller_pwr_on() {
    return true;
}

static bool __cdecl ac_io_i36i_ps3_controller_pwr_off() {
    return true;
}

static bool __cdecl ac_io_i36i_create_get_status_thread() {
    return true;
}

static bool __cdecl ac_io_i36i_destroy_get_status_thread() {
    return true;
}

static bool __cdecl ac_io_i36i_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    memset(STATUS_BUFFER, 0, sizeof(STATUS_BUFFER));

    // Winning Eleven
    if (avs::game::is_model({ "KCK", "NCK" })) {
        // TODO
    }

    // success
    return true;
}

static bool __cdecl ac_io_i36i_get_control_status_buffer(uint8_t *buffer) {
    memcpy(buffer, STATUS_BUFFER, sizeof(STATUS_BUFFER));
    return true;
}

static bool __cdecl ac_io_i36i_usb_controller_bus_IO() {
    return true;
}

static bool __cdecl ac_io_i36i_usb_controller_bus_PC() {
    return true;
}

static bool __cdecl ac_io_i36i_req_get_usb_desc(int a1) {
    return true;
}

static bool __cdecl ac_io_i36i_req_get_usb_desc_isfinished(
        int a1, uint32_t *a2, int a3, uint32_t *out_size, uint8_t *in_data, unsigned int in_size) {

    // DualShock 3 device descriptor
    static uint8_t DS3_DESC[] {
        0x12,        // bLength
        0x01,        // bDescriptorType (Device)
        0x00, 0x02,  // bcdUSB 2.00
        0x00,        // bDeviceClass (Use class information in the Interface Descriptors)
        0x00,        // bDeviceSubClass
        0x00,        // bDeviceProtocol
        0x40,        // bMaxPacketSize0 64
        0x4C, 0x05,  // idVendor 0x054C
        0x68, 0x02,  // idProduct 0x0268
        0x00, 0x01,  // bcdDevice 1.00
        0x01,        // iManufacturer (String Index)
        0x02,        // iProduct (String Index)
        0x00,        // iSerialNumber (String Index)
        0x01,        // bNumConfigurations 1
    };

    // copy descriptor to buffer
    *out_size = MIN(sizeof(DS3_DESC), in_size);
    memcpy(in_data, DS3_DESC, *out_size);

    // we apparently need this too
    *a2 = 3;

    // return success
    return true;
}

/*
 * Module stuff
 */
acio::I36IModule::I36IModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("I36I", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::I36IModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_i36i_ps3_controller_pwr_on);
    ACIO_MODULE_HOOK(ac_io_i36i_ps3_controller_pwr_off);
    ACIO_MODULE_HOOK(ac_io_i36i_create_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_i36i_destroy_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_i36i_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_i36i_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_i36i_usb_controller_bus_IO);
    ACIO_MODULE_HOOK(ac_io_i36i_usb_controller_bus_PC);
    ACIO_MODULE_HOOK(ac_io_i36i_req_get_usb_desc);
    ACIO_MODULE_HOOK(ac_io_i36i_req_get_usb_desc_isfinished);
}
