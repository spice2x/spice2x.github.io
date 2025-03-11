#include "pix.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "games/museca/io.h"
#include "games/bbc/io.h"
#include "util/utils.h"
#include "avs/game.h"

using namespace GameAPI;

// static stuff
static int ACIO_PIX_WARMUP = 0;
static uint8_t STATUS_BUFFER[60];
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static char __cdecl ac_io_pix_begin(char a1, long long a2, int a3, int a4, int a5, int a6) {
    return 1;
}

static char __cdecl ac_io_pix_begin_get_status(int a1, int a2) {
    return 1;
}

static char __cdecl ac_io_pix_end(int a1) {
    return 1;
}

static char __cdecl ac_io_pix_end_get_status(int a1) {
    return 1;
}

static char __cdecl ac_io_pix_get_firmware_update_device_index(int a1) {
    return 1;
}

static char __cdecl ac_io_pix_get_node_no(int a1, int a2) {
    return 1;
}

static void *__cdecl ac_io_pix_get_recv_log(long long a1, void *a2, int a3) {
    return a2;
}

static void *__cdecl ac_io_pix_get_rs232c_status(void *a1, int a2) {
    return a1;
}

static void *__cdecl ac_io_pix_get_send_log(long long a1, void *a2, int a3) {
    return a2;
}

static char __cdecl ac_io_pix_get_version(void *a1, int a2, int a3) {
    return 1;
}

static const char *__cdecl ac_io_pix_get_version_string() {
    static const char *version = "1.25.0";
    return version;
}

static char __cdecl ac_io_pix_go_firmware_update(int a1) {
    return 1;
}

static char __cdecl ac_io_pix_is_active(int a1, int a2) {
    return (char) (++ACIO_PIX_WARMUP > 601 ? 1 : 0);
}

static char __cdecl ac_io_pix_is_active2(int a1, int *a2, int a3) {
    ACIO_PIX_WARMUP = 601;
    *a2 = 6;
    return 1;
}

static char __cdecl ac_io_pix_is_active_device(int a1, int a2) {
    return (char) (a1 != 5);
}

static long long __cdecl ac_io_pix_reset(int a1) {
    return a1;
}

static bool __cdecl ac_io_pix_rvol_change_expand_mode(char a1) {
    return true;
}

static long long __cdecl ac_io_pix_rvol_control_led_bright(uint32_t led_field, uint8_t brightness) {

    // MUSECA
    if (avs::game::is_model("PIX")) {

        // get lights
        auto &lights = games::museca::get_lights();

        // control mapping
        static int mapping[] = {
                games::museca::Lights::Spinner1R,
                games::museca::Lights::Spinner1G,
                games::museca::Lights::Spinner1B,
                games::museca::Lights::Spinner2R,
                games::museca::Lights::Spinner2G,
                games::museca::Lights::Spinner2B,
                games::museca::Lights::Spinner3R,
                games::museca::Lights::Spinner3G,
                games::museca::Lights::Spinner3B,
                games::museca::Lights::Spinner4R,
                games::museca::Lights::Spinner4G,
                games::museca::Lights::Spinner4B,
                games::museca::Lights::Spinner5R,
                games::museca::Lights::Spinner5G,
                games::museca::Lights::Spinner5B,
                games::museca::Lights::TitleR,
                games::museca::Lights::TitleG,
                games::museca::Lights::TitleB
        };

        // write light
        float value = brightness > 127.f ? 1.f : brightness / 127.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (mapping[i] >= 0 && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at((size_t) mapping[i]), value);
            }
        }
    }

    // BISHI BASHI CHANNEL
    if (avs::game::is_model("R66")) {

        // get lights
        auto &lights = games::bbc::get_lights();

        // control mapping
        static int mapping[] = {
                games::bbc::Lights::P1_DISC_R,
                games::bbc::Lights::P1_DISC_G,
                games::bbc::Lights::P1_DISC_B,
                games::bbc::Lights::P3_DISC_R,
                games::bbc::Lights::P3_DISC_G,
                games::bbc::Lights::P3_DISC_B,
                games::bbc::Lights::P2_DISC_R,
                games::bbc::Lights::P2_DISC_G,
                games::bbc::Lights::P2_DISC_B,
                games::bbc::Lights::P4_DISC_R,
                games::bbc::Lights::P4_DISC_G,
                games::bbc::Lights::P4_DISC_B,
                games::bbc::Lights::P1_R,
                games::bbc::Lights::P1_B,
                -1, -1, -1, -1, -1, -1,
                games::bbc::Lights::P2_R,
                games::bbc::Lights::P2_B,
                games::bbc::Lights::P3_R,
                games::bbc::Lights::P3_B,
                games::bbc::Lights::P4_R,
                games::bbc::Lights::P4_B,
        };

        // write light
        float value = brightness / 255.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (mapping[i] >= 0 && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at((size_t) mapping[i]), value);
            }
        }
    }

    // return success
    return 1;
}

static long long __cdecl ac_io_pix_rvol_control_reset() {
    return 1;
}

static bool __cdecl ac_io_pix_rvol_create_get_status_thread() {
    return true;
}

static long long __cdecl ac_io_pix_rvol_destroy_get_status_thread() {
    return 1;
}

static void *__cdecl ac_io_pix_rvol_get_control_status_buffer(void *a1) {

    // copy buffer
    return memcpy(a1, STATUS_BUFFER, sizeof(STATUS_BUFFER));
}

static bool __cdecl ac_io_pix_rvol_get_watchdog_status() {
    return true;
}

static short __cdecl ac_io_pix_rvol_get_watchdog_time_min() {
    return 0;
}

static short __cdecl ac_io_pix_rvol_get_watchdog_time_now() {
    return 0;
}

static bool __cdecl ac_io_pix_rvol_modify_auto_input_get(long long a1, long long a2) {
    return true;
}

static char __cdecl ac_io_pix_rvol_req_get_control_status(DWORD *a1) {
    *a1 = 1;
    return 1;
}

static bool __cdecl ac_io_pix_rvol_req_volume_control(char a1, char a2, char a3, char a4) {
    return true;
}

static bool __cdecl ac_io_pix_rvol_req_volume_control_isfinished(DWORD *a1) {
    *a1 = 5;
    return true;
}

static long long __cdecl ac_io_pix_rvol_set_framing_err_packet_send_interval(long long a1) {
    return a1;
}

static bool __cdecl ac_io_pix_rvol_set_watchdog_time(short a1) {
    return true;
}

static bool __cdecl ac_io_pix_rvol_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    memset(STATUS_BUFFER, 0, sizeof(STATUS_BUFFER));

    // MUSECA
    if (avs::game::is_model("PIX")) {

        // get input
        auto &buttons = games::museca::get_buttons();

        // get slowdown status
        bool slowdown = Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::AnalogSlowdown));

        // update disk buttons
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk1Press)))
            ARRAY_SETB(STATUS_BUFFER, 107);
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk2Press)))
            ARRAY_SETB(STATUS_BUFFER, 104);
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk3Press)))
            ARRAY_SETB(STATUS_BUFFER, 123);
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk4Press)))
            ARRAY_SETB(STATUS_BUFFER, 42);
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk5Press)))
            ARRAY_SETB(STATUS_BUFFER, 44);

        // foot pedal (inverted)
        if (!Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::FootPedal)))
            ARRAY_SETB(STATUS_BUFFER, 43);

        // update analogs
        static uint8_t analogs[5] = { 0, 0, 0, 0, 0 };
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk1Minus))) {
            analogs[0] -= slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk1Plus))) {
            analogs[0] += slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk2Minus))) {
            analogs[1] -= slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk2Plus))) {
            analogs[1] += slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk3Minus))) {
            analogs[2] -= slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk3Plus))) {
            analogs[2] += slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk4Minus))) {
            analogs[3] -= slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk4Plus))) {
            analogs[3] += slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk5Minus))) {
            analogs[4] -= slowdown ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Disk5Plus))) {
            analogs[4] += slowdown ? 3 : 12;
        }

        // raw input analogs
        auto &analog_list = games::museca::get_analogs();
        size_t analog_mapping[] = {
                games::museca::Analogs::Disk1,
                games::museca::Analogs::Disk2,
                games::museca::Analogs::Disk3,
                games::museca::Analogs::Disk4,
                games::museca::Analogs::Disk5,
        };
        uint8_t set_values[5];
        std::copy(std::begin(analogs), std::end(analogs), std::begin(set_values));
        for (size_t i = 0; i < 5; i++) {
            auto &analog_item = analog_list.at(analog_mapping[i]);
            if (analog_item.isSet()) {
                set_values[i] = analogs[i] + (uint8_t) (Analogs::getState(RI_MGR, analog_item) * 255.99f);
            }
        }

        // set analogs
        for (int i = 0; i < 5; i++)
            STATUS_BUFFER[20 + i] = set_values[i];
    }

    // BISHI BASHI CHANNEL
    if (avs::game::is_model("R66")) {

        // get input
        auto &buttons = games::bbc::get_buttons();
        auto &analogs = games::bbc::get_analogs();

        // get slowdown status
        bool slowdown1 = Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P1_DiskSlowdown));
        bool slowdown2 = Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P2_DiskSlowdown));
        bool slowdown3 = Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P3_DiskSlowdown));
        bool slowdown4 = Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P4_DiskSlowdown));

        // update buttons
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P1_R)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 44);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P1_G)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 107);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P1_B)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 41);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P2_R)) == Buttons::State::BUTTON_NOT_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 39);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P2_G)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 123);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P2_B)) == Buttons::State::BUTTON_NOT_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 55);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P3_R)) == Buttons::State::BUTTON_NOT_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 71);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P3_G)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 104);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P3_B)) == Buttons::State::BUTTON_NOT_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 87);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P4_R)) == Buttons::State::BUTTON_NOT_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 103);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P4_G)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 42);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P4_B)) == Buttons::State::BUTTON_NOT_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 119);
        }

        // update analogs
        static uint8_t analog_states[4] = { 0, 0, 0, 0 };
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P1_DiskMinus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[0] -= slowdown1 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P1_DiskPlus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[0] += slowdown1 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P2_DiskMinus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[1] -= slowdown2 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P2_DiskPlus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[1] += slowdown2 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P3_DiskMinus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[2] -= slowdown3 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P3_DiskPlus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[2] += slowdown3 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P4_DiskMinus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[3] -= slowdown4 ? 3 : 12;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::P4_DiskPlus)) == Buttons::State::BUTTON_PRESSED) {
            analog_states[3] += slowdown4 ? 3 : 12;
        }

        // raw input analogs
        uint8_t set_values[4];
        size_t analog_mappings[] = {
                games::bbc::Analogs::P1_Disk,
                games::bbc::Analogs::P2_Disk,
                games::bbc::Analogs::P3_Disk,
                games::bbc::Analogs::P4_Disk,
        };
        std::copy(std::begin(analog_states), std::end(analog_states), std::begin(set_values));
        for (size_t i = 0; i < 4; i++) {
            auto &analog_item = analogs.at(analog_mappings[i]);
            if (analog_item.isSet()) {
                set_values[i] = analog_states[i] + (uint8_t) (Analogs::getState(RI_MGR, analog_item) * 255.99f);
            }
        }

        // flip disk 2/3
        set_values[1] ^= set_values[2];
        set_values[2] ^= set_values[1];
        set_values[1] ^= set_values[2];

        // set analogs
        for (int i = 0; i < 4; i++) {
            STATUS_BUFFER[20 + i] = set_values[i];
        }
    }

    // success
    return true;
}

static void __cdecl ac_io_pix_rvol_watchdog_off() {
}

static void *__cdecl ac_io_pix_secplug_set_encodedpasswd(void *a1, unsigned int a2) {
    return a1;
}

static void *__cdecl ac_io_pix_set_get_status_device(void *a1, int a2) {
    return a1;
}

static void *__cdecl ac_io_pix_set_soft_watch_dog(void *a1, int a2) {
    return a1;
}

static char __cdecl ac_io_pix_soft_watch_dog_off(int a1) {
    return 1;
}

static char __cdecl ac_io_pix_soft_watch_dog_on(int a1) {
    return 1;
}

static char __cdecl ac_io_pix_update(long long a1) {

    // flush outputs
    RI_MGR->devices_flush_output();

    return 1;
}

static const char* __cdecl ac_io_pix_version() {
    static const char *version = "Version: 1.25.0\nBuild Date: Sep 20 2016 15:16:13\nBuild Host: DEMETER\n";
    return version;
}

/*
 * Module stuff
 */

acio::PIXModule::PIXModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("PIX", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::PIXModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_pix_begin);
    ACIO_MODULE_HOOK(ac_io_pix_begin_get_status);
    ACIO_MODULE_HOOK(ac_io_pix_end);
    ACIO_MODULE_HOOK(ac_io_pix_end_get_status);
    ACIO_MODULE_HOOK(ac_io_pix_get_firmware_update_device_index);
    ACIO_MODULE_HOOK(ac_io_pix_get_node_no);
    ACIO_MODULE_HOOK(ac_io_pix_get_recv_log);
    ACIO_MODULE_HOOK(ac_io_pix_get_rs232c_status);
    ACIO_MODULE_HOOK(ac_io_pix_get_send_log);
    ACIO_MODULE_HOOK(ac_io_pix_get_version);
    ACIO_MODULE_HOOK(ac_io_pix_get_version_string);
    ACIO_MODULE_HOOK(ac_io_pix_go_firmware_update);
    ACIO_MODULE_HOOK(ac_io_pix_is_active);
    ACIO_MODULE_HOOK(ac_io_pix_is_active2);
    ACIO_MODULE_HOOK(ac_io_pix_is_active_device);
    ACIO_MODULE_HOOK(ac_io_pix_reset);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_change_expand_mode);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_control_led_bright);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_control_reset);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_create_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_destroy_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_get_watchdog_status);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_get_watchdog_time_min);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_get_watchdog_time_now);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_modify_auto_input_get);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_req_get_control_status);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_req_volume_control);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_req_volume_control_isfinished);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_set_framing_err_packet_send_interval);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_pix_rvol_watchdog_off);
    ACIO_MODULE_HOOK(ac_io_pix_secplug_set_encodedpasswd);
    ACIO_MODULE_HOOK(ac_io_pix_set_get_status_device);
    ACIO_MODULE_HOOK(ac_io_pix_set_soft_watch_dog);
    ACIO_MODULE_HOOK(ac_io_pix_soft_watch_dog_off);
    ACIO_MODULE_HOOK(ac_io_pix_soft_watch_dog_on);
    ACIO_MODULE_HOOK(ac_io_pix_update);
    ACIO_MODULE_HOOK(ac_io_pix_version);
}
