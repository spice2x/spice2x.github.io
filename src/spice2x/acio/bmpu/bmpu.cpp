#include "bmpu.h"

#include "acio/icca/icca.h"
#include "avs/game.h"
#include "cfg/api.h"
#include "cfg/light.h"
#include "games/bbc/io.h"
#include "games/dea/io.h"
#include "games/ftt/io.h"
#include "games/museca/io.h"
#include "games/silentscope/io.h"
#include "launcher/launcher.h"
#include "misc/eamuse.h"

using namespace GameAPI;

// state
static uint8_t STATUS_BUFFER[64] {};
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static char __cdecl ac_io_bmpu_consume_coinstock(int a1, int a2) {
    eamuse_coin_consume_stock();
    return 1;
}

static int __cdecl ac_io_bmpu_control_1p_start_led_off() {

    // dance evolution
    if (avs::game::is_model("KDM")) {
        auto &lights = games::dea::get_lights();
        Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::P1Start), 0.f);
    }

    return 1;
}

static int __cdecl ac_io_bmpu_control_1p_start_led_on() {

    // dance evolution
    if (avs::game::is_model("KDM")) {
        auto &lights = games::dea::get_lights();
        Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::P1Start), 1.f);
    }

    return 1;
}

static int __cdecl ac_io_bmpu_control_2p_start_led_off() {

    // dance evolution
    if (avs::game::is_model("KDM")) {
        auto &lights = games::dea::get_lights();
        Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::P2Start), 0.f);
    }

    return 1;
}

static int __cdecl ac_io_bmpu_control_2p_start_led_on() {

    // dance evolution
    if (avs::game::is_model("KDM")) {
        auto &lights = games::dea::get_lights();
        Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::P2Start), 1.f);
    }

    return 1;
}

static int __cdecl ac_io_bmpu_control_coin_blocker_close() {
    eamuse_coin_set_block(true);
    return 1;
}

static int __cdecl ac_io_bmpu_control_coin_blocker_open() {
    eamuse_coin_set_block(false);
    return 1;
}

static bool __cdecl ac_io_bmpu_control_led_bright(uint32_t led_field, uint8_t brightness) {

    // MUSECA
    if (avs::game::is_model("PIX")) {

        // get lights
        auto &lights = games::museca::get_lights();

        // control mapping
        static const int mapping[] = {
                games::museca::Lights::UnderLED3G,
                games::museca::Lights::UnderLED3R,
                games::museca::Lights::UnderLED2B,
                games::museca::Lights::UnderLED2G,
                games::museca::Lights::UnderLED2R,
                games::museca::Lights::UnderLED1B,
                games::museca::Lights::UnderLED1G,
                games::museca::Lights::UnderLED1R,
                -1, -1, -1, -1,
                games::museca::Lights::SideB,
                games::museca::Lights::SideG,
                games::museca::Lights::SideR,
                games::museca::Lights::UnderLED3B,
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
                games::bbc::Lights::UNDER_LED3_G,
                games::bbc::Lights::UNDER_LED3_R,
                games::bbc::Lights::UNDER_LED2_B,
                games::bbc::Lights::UNDER_LED2_G,
                games::bbc::Lights::UNDER_LED2_R,
                games::bbc::Lights::UNDER_LED1_B,
                games::bbc::Lights::UNDER_LED1_G,
                games::bbc::Lights::UNDER_LED1_R,
                -1, -1, -1, -1,
                games::bbc::Lights::IC_CARD_B,
                games::bbc::Lights::IC_CARD_G,
                games::bbc::Lights::IC_CARD_R,
                games::bbc::Lights::UNDER_LED3_B,
        };

        // write light
        float value = brightness > 127.f ? 1.f : brightness / 127.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (mapping[i] >= 0 && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at((size_t) mapping[i]), value);
            }
        }
    }

    // FutureTomTom
    if (avs::game::is_model("MMD")) {

        // get lights
        auto &lights = games::ftt::get_lights();

        // control mapping
        static int mapping[] = {
                games::ftt::Lights::Pad3_G,
                games::ftt::Lights::Pad3_R,
                games::ftt::Lights::Pad2_B,
                games::ftt::Lights::Pad2_G,
                games::ftt::Lights::Pad2_R,
                games::ftt::Lights::Pad1_B,
                games::ftt::Lights::Pad1_G,
                games::ftt::Lights::Pad1_R,
                -1, -1, -1, -1,
                games::ftt::Lights::Pad4_B,
                games::ftt::Lights::Pad4_G,
                games::ftt::Lights::Pad4_R,
                games::ftt::Lights::Pad3_B,
        };

        // write light
        float value = brightness > 127.f ? 1.f : brightness / 127.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (mapping[i] >= 0 && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at((size_t) mapping[i]), value);
            }
        }
    }

    // Dance Evolution
    if (avs::game::is_model("KDM")) {

        // get lights
        auto &lights = games::dea::get_lights();

        // control mapping
        static int mapping[] = {
                -1,
                -1,
                -1,
                -1,
                -1,
                -1,
                games::dea::Lights::P2LRButton,
                games::dea::Lights::P1LRButton,
                -1,
                games::dea::Lights::TitleB,
                games::dea::Lights::TitleR,
                games::dea::Lights::TitleG,
                -1,
        };

        // write light
        float value = brightness > 128.f ? 1.f : brightness / 128.f;
        for (size_t i = 0; i < std::size(mapping); i++)
            if (mapping[i] >= 0 && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at((size_t) mapping[i]), value);
            }
    }

    // return success
    return true;
}

static bool __cdecl ac_io_bmpu_control_led_bright_pack(int a1, int a2, int a3) {
    // TODO(felix): NDD lights

    return true;
}

static bool __cdecl ac_io_bmpu_create_get_status_thread() {
    return true;
}

static char __cdecl ac_io_bmpu_current_coinstock(int a1, int *a2) {
    *a2 = eamuse_coin_get_stock();
    return 1;
}

static bool __cdecl ac_io_bmpu_destroy_get_status_thread() {
    return true;
}

static char __cdecl ac_io_bmpu_get_control_status_buffer(void *buffer) {
    size_t buffer_len = 0;

    if (avs::game::is_model({ "KDM", "MMD" })) {
        buffer_len = sizeof(STATUS_BUFFER);
    } else if (avs::game::is_model("PIX")) {
        buffer_len = 16;
    } else if (avs::game::is_model("R66")) {
        buffer_len = 56;
    } else if (avs::game::is_model("NDD")) {
        buffer_len = 56;
    }

    if (buffer_len > 0) {
        memcpy(buffer, &STATUS_BUFFER, buffer_len);
    }

    // success
    return true;
}

static char *__cdecl ac_io_bmpu_get_softwareid(char *a1) {
    *a1 = 0;
    return a1;
}

static char *__cdecl ac_io_bmpu_get_systemid(char *a1) {
    *a1 = 0;
    return a1;
}

static char __cdecl ac_io_bmpu_init_outport() {
    return 1;
}

static char __cdecl ac_io_bmpu_lock_coincounter(signed int a1) {
    return 1;
}

static char __cdecl ac_io_bmpu_req_secplug_check_isfinished(DWORD *a1) {
    return 1;
}

static char __cdecl ac_io_bmpu_req_secplug_check_softwareplug(char *a1) {
    return 1;
}

static char __cdecl ac_io_bmpu_req_secplug_check_systemplug() {
    return 1;
}

static char __cdecl ac_io_bmpu_req_secplug_missing_check() {
    return 1;
}

static int __cdecl ac_io_bmpu_req_secplug_missing_check_isfinished(DWORD *a1) {
    return 1;
}

static int __cdecl ac_io_bmpu_set_outport_led(uint8_t *data1, uint8_t *data2) {

    // dance evolution
    if (avs::game::is_model("KDM")) {

        // get lights
        auto &lights = games::dea::get_lights();

        // mapping
        static const size_t mapping[] {
                games::dea::Lights::SideUpperLeftR,
                games::dea::Lights::SideUpperLeftG,
                games::dea::Lights::SideUpperLeftB,
                games::dea::Lights::SideLowerLeft1R,
                games::dea::Lights::SideLowerLeft1G,
                games::dea::Lights::SideLowerLeft1B,
                games::dea::Lights::SideLowerLeft2R,
                games::dea::Lights::SideLowerLeft2G,
                games::dea::Lights::SideLowerLeft2B,
                games::dea::Lights::SideLowerLeft3R,
                games::dea::Lights::SideLowerLeft3G,
                games::dea::Lights::SideLowerLeft3B,
                games::dea::Lights::SideUpperRightR,
                games::dea::Lights::SideUpperRightG,
                games::dea::Lights::SideUpperRightB,
                games::dea::Lights::SideLowerRight1R,
                games::dea::Lights::SideLowerRight1G,
                games::dea::Lights::SideLowerRight1B,
                games::dea::Lights::SideLowerRight2R,
                games::dea::Lights::SideLowerRight2G,
                games::dea::Lights::SideLowerRight2B,
                games::dea::Lights::SideLowerRight3R,
                games::dea::Lights::SideLowerRight3G,
                games::dea::Lights::SideLowerRight3B,
        };

        // write lights
        for (size_t i = 0; i < std::size(mapping); i++) {
            float brightness = data1[i * 2] / 255.f;
            Lights::writeLight(RI_MGR, lights.at(mapping[i]), brightness);
        }
    }

    // success
    return true;
}

static int __cdecl ac_io_bmpu_set_output_mode(__int16 a1) {
    return 1;
}

static char __cdecl ac_io_bmpu_unlock_coincounter(int a1) {
    return 1;
}

static bool __cdecl ac_io_bmpu_update_control_status_buffer() {
    unsigned int control_data = 0;

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // DEA
    if (avs::game::is_model("KDM")) {

        // keypad mirror fix
        acio::ICCA_FLIP_ROWS = true;

        // get buttons
        auto &buttons = games::dea::get_buttons();

        // get control data
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::Test))) {
            control_data |= 0xF0000000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::Service))) {
            control_data |= 0x0F000000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::P1Start))) {
            control_data |= 0x00000001;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::P1Left))) {
            control_data |= 0x00000008;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::P1Right))) {
            control_data |= 0x00000010;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::P2Start))) {
            control_data |= 0x00000100;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::P2Left))) {
            control_data |= 0x00000800;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::dea::Buttons::P2Right))) {
            control_data |= 0x00001000;
        }

        // set control data
        auto buffer = reinterpret_cast<unsigned int *>(STATUS_BUFFER);
        for (size_t i = 0; i < 16; i++) {
            buffer[i] = control_data;
        }
    }

    // FutureTomTom
    if (avs::game::is_model("MMD")) {

        // keypad mirror fix
        acio::ICCA_FLIP_ROWS = true;

        // get buttons
        auto &buttons = games::ftt::get_buttons();

        // get control data
        if (Buttons::getState(RI_MGR, buttons.at(games::ftt::Buttons::Service))) {
            control_data |= 0x0F000000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ftt::Buttons::Test))) {
            control_data |= 0xF0000000;
        }

        // set control data
        auto buffer = reinterpret_cast<unsigned int *>(STATUS_BUFFER);
        for (size_t i = 0; i < 16; i++) {
            buffer[i] = control_data;
        }
    }

    // MUSECA
    if (avs::game::is_model("PIX")) {

        // get buttons
        auto &buttons = games::museca::get_buttons();

        // get control data
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Service))) {
            control_data |= 0x0F000000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Test))) {
            control_data |= 0xF0000000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::museca::Buttons::Start))) {
            control_data |= 0x00000001;
        }

        // set control data
        auto buffer = reinterpret_cast<unsigned int *>(STATUS_BUFFER);
        for (size_t i = 0; i < 4; i++) {
            buffer[i] = control_data;
        }
    }

    // BISHI BASHI CHANNEL
    if (avs::game::is_model("R66")) {

        // get buttons
        auto &buttons = games::bbc::get_buttons();

        // get control data
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::Service))) {
            control_data |= 0x0F000000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bbc::Buttons::Test))) {
            control_data |= 0xF0000000;
        }

        // set control data
        auto buffer = reinterpret_cast<unsigned int *>(STATUS_BUFFER);
        for (size_t i = 0; i < 4; i++) {
            buffer[i] = control_data;
        }
    }

    // Silent Scope Bone Eater
    if (avs::game::is_model("NDD")) {

        // clear state
        memset(STATUS_BUFFER, 0, 56);

        // get buttons
        auto &buttons = games::silentscope::get_buttons();

        // get control data
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::TEST))) {
            STATUS_BUFFER[7] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::SERVICE))) {
            STATUS_BUFFER[7] |= 0x2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::COIN_MECH))) {
            STATUS_BUFFER[7] |= 0x1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::START))) {
            STATUS_BUFFER[5] |= 0x1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::UP))) {
            STATUS_BUFFER[5] |= 0x2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::DOWN))) {
            STATUS_BUFFER[5] |= 0x4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::LEFT))) {
            STATUS_BUFFER[5] |= 0x8;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::RIGHT))) {
            STATUS_BUFFER[5] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::SCOPE_RIGHT))) {
            STATUS_BUFFER[4] |= 0x80;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::SCOPE_LEFT))) {
            STATUS_BUFFER[4] |= 0x40;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::silentscope::Buttons::GUN_PRESSED))) {
            STATUS_BUFFER[4] |= 0x20;
        }

        // joy stick raw input
        auto &analogs = games::silentscope::get_analogs();
        unsigned short joy_x = 0x7FFF;
        unsigned short joy_y = 0x7FFF;
        if (analogs.at(games::silentscope::Analogs::GUN_X).isSet()) {
            joy_x = (unsigned short) (Analogs::getState(RI_MGR, analogs.at(games::silentscope::Analogs::GUN_X)) * USHRT_MAX);
        }
        if (analogs.at(games::silentscope::Analogs::GUN_Y).isSet()) {
            joy_y = (unsigned short) (Analogs::getState(RI_MGR, analogs.at(games::silentscope::Analogs::GUN_Y)) * USHRT_MAX);
        }

        // invert X axis
        joy_x = USHRT_MAX - joy_x;

        STATUS_BUFFER[8] = HIBYTE(joy_x);
        STATUS_BUFFER[9] = LOBYTE(joy_x);
        STATUS_BUFFER[10] = HIBYTE(joy_y);
        STATUS_BUFFER[11] = LOBYTE(joy_y);
    }

    // success
    return true;
}

static bool __cdecl ac_io_bmpu_set_watchdog_time(char a1) {
    return true;
}

static char __cdecl ac_io_bmpu_get_watchdog_time_min() {
    return 0;
}

static char __cdecl ac_io_bmpu_get_watchdog_time_now() {
    return 0;
}

static void __cdecl ac_io_bmpu_watchdog_off() {
}

/*
 * Module stuff
 */

acio::BMPUModule::BMPUModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("BMPU", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::BMPUModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_bmpu_consume_coinstock);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_1p_start_led_off);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_1p_start_led_on);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_2p_start_led_off);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_2p_start_led_on);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_coin_blocker_close);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_coin_blocker_open);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_led_bright);
    ACIO_MODULE_HOOK(ac_io_bmpu_control_led_bright_pack);
    ACIO_MODULE_HOOK(ac_io_bmpu_create_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_bmpu_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_bmpu_destroy_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_bmpu_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_bmpu_get_softwareid);
    ACIO_MODULE_HOOK(ac_io_bmpu_get_systemid);
    ACIO_MODULE_HOOK(ac_io_bmpu_init_outport);
    ACIO_MODULE_HOOK(ac_io_bmpu_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_bmpu_req_secplug_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_bmpu_req_secplug_check_softwareplug);
    ACIO_MODULE_HOOK(ac_io_bmpu_req_secplug_check_systemplug);
    ACIO_MODULE_HOOK(ac_io_bmpu_req_secplug_missing_check);
    ACIO_MODULE_HOOK(ac_io_bmpu_req_secplug_missing_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_bmpu_set_outport_led);
    ACIO_MODULE_HOOK(ac_io_bmpu_set_output_mode);
    ACIO_MODULE_HOOK(ac_io_bmpu_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_bmpu_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_bmpu_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_bmpu_get_watchdog_time_min);
    ACIO_MODULE_HOOK(ac_io_bmpu_get_watchdog_time_now);
    ACIO_MODULE_HOOK(ac_io_bmpu_watchdog_off);
}
