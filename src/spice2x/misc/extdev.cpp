#include "extdev.h"
#include <cmath>
#include <string>
#include <optional>
#include <windows.h>
#include "util/utils.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/circular_buffer.h"
#include "util/socd_cleaner.h"
#include "util/time.h"
#include "misc/eamuse.h"
#include "cfg/api.h"
#include "acio/icca/icca.h"
#include "games/gitadora/gitadora.h"
#include "games/gitadora/io.h"
#include "avs/game.h"

using namespace GameAPI;

#if SPICE64
typedef uint64_t LED_INDEX;
#else
typedef uint32_t LED_INDEX;
#endif

// card unit
static size_t EXTDEV_CARDUNIT_COUNT = 0;
static circular_buffer<int> EXTDEV_CARDUNIT_KEY[2] = {
        circular_buffer<int>(32),
        circular_buffer<int>(32)
};
static circular_buffer<const char*> EXTDEV_CARDUNIT_KEY_STR[2] = {
        circular_buffer<const char*>(32),
        circular_buffer<const char*>(32)
};
static int EXTDEV_CARDUNIT_EJECT[2] = {0, 0};
static bool EXTDEV_CARD_IN[2] = {false, false};
static bool EXTDEV_CARD_PRESSED[2] = {false, false};
static uint8_t EXTDEV_CARD[2][8];
static bool EXTDEV_CARDUNIT_TENKEY_STATE[2][12]{};
static std::string EXTDEV_CARDUNIT_TENKEY_STRINGS[] = {
        "0",
        "1",
        "2",
        "3",
        "4",
        "5",
        "6",
        "7",
        "8",
        "9",
        "RETURN", // enter
        "00" // double zero
};
static unsigned int EXTDEV_CARDUNIT_TENKEY_EAMUSE_MAPPING[] = {
        0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4
};
static unsigned int EXTDEV_CARDUNIT_TENKEY_NUMS[] {
        0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4
};

// GFDM
static int32_t GFDM_DM_ATTACK_BORDER_VALUE[] = {
        0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50
};
static bool GFDM_GF_PICK_STATE_UP[2]{};
static bool GFDM_GF_PICK_STATE_DOWN[2]{};

// state
static HINSTANCE EXTDEV_INSTANCE;
static std::string EXTDEV_INSTANCE_NAMES[] = {
                "ext_dev.dll",
                "libextio.dll",
                "libcardunit.dll",
                "libledunit.dll",
                "libgfdm_unit2.dll",
                "libshare-pj.dll",
};

static void set_led_state(games::gitadora::Lights::gitadora_lights_t light, bool led_on);
static void set_led_value(games::gitadora::Lights::gitadora_lights_t light, uint8_t value);

static long __cdecl gitadora_led_common(long index, uint8_t *data);

static void __cdecl cardunit_boot2(int a1, int a2, int a3) {

    // default reader count to 1
    EXTDEV_CARDUNIT_COUNT = 1;

    // exceptions for games with two readers
    if (avs::game::is_model({ "J33", "K33", "L33", "M32" })) {
        EXTDEV_CARDUNIT_COUNT = 2;
    }
}

static void __cdecl cardunit_boot(int a1, int a2) {
    cardunit_boot2(1, a1, a2);
}

static int __cdecl cardunit_boot_initialize() {
    return 0;
}

static void __cdecl cardunit_boot_no_slot_type(int a1, int a2) {
    cardunit_boot2(1, a1, a2);
}

static void __cdecl cardunit_card_eject(int unit) {
    EXTDEV_CARDUNIT_EJECT[unit] = 1;
}

static int __cdecl cardunit_card_eject_complete(int unit) {
    EXTDEV_CARD_IN[unit] = false;
    return EXTDEV_CARDUNIT_EJECT[unit];
}

static int __cdecl cardunit_card_eject_wait(int unit) {
    return EXTDEV_CARDUNIT_EJECT[unit];
}

static int __cdecl cardunit_card_read2(int unit, void *card, int *status) {

    // clear the eject flag
    EXTDEV_CARDUNIT_EJECT[unit] = 0;

    // check if a card was inserted
    if (EXTDEV_CARD_IN[unit]) {

        // copy card, return success
        memcpy(card, EXTDEV_CARD[unit], 8);
        *status = is_card_uid_felica(EXTDEV_CARD[unit]) ? 2 : 1;
        return 0;

    } else {

        // tried to read card with no card inserted, return fail
        memset(card, 0, 8);
        *status = 0;
        return 1;
    }
}

static int __cdecl cardunit_card_read(int unit, void *card) {
    return cardunit_card_read2(unit, card, nullptr);
}

static void __cdecl cardunit_card_ready(int unit) {
    EXTDEV_CARDUNIT_EJECT[unit] = 0;
}

static int __cdecl cardunit_card_sensor() {
    return 0;
}

static int __cdecl cardunit_card_sensor_raw(int a1) {
    return 1;
}

static void __cdecl cardunit_update() {

    // update all units
    for (size_t unit = 0; unit < EXTDEV_CARDUNIT_COUNT; unit++) {
        bool kb_insert_press = false;

        // eamio keypress
        kb_insert_press |= static_cast<bool>(eamuse_get_keypad_state(unit) & (1 << EAM_IO_INSERT));

        // update card inserts
        if (eamuse_card_insert_consume((int) EXTDEV_CARDUNIT_COUNT, unit) ||
                (kb_insert_press && !EXTDEV_CARD_PRESSED[unit])) {
            EXTDEV_CARD_PRESSED[unit] = true;
            if (!EXTDEV_CARD_IN[unit]) {
                EXTDEV_CARD_IN[unit] = true;
                eamuse_get_card((int) EXTDEV_CARDUNIT_COUNT, unit, EXTDEV_CARD[unit]);
            }
        } else
            EXTDEV_CARD_PRESSED[unit] = false;

        // get eamu key states
        uint16_t eamu_state = eamuse_get_keypad_state(unit);

        // iterate all keys
        for (int i = 0; i < 12; i++) {

            // check if key is pressed
            if (eamu_state & (1 << EXTDEV_CARDUNIT_TENKEY_EAMUSE_MAPPING[i])) {

                // check if key was pressed before
                if (!EXTDEV_CARDUNIT_TENKEY_STATE[unit][i]) {

                    // remember key press
                    EXTDEV_CARDUNIT_TENKEY_STATE[unit][i] = true;

                    // set last key
                    EXTDEV_CARDUNIT_KEY[unit].put(EXTDEV_CARDUNIT_TENKEY_NUMS[i]);
                    EXTDEV_CARDUNIT_KEY_STR[unit].put(EXTDEV_CARDUNIT_TENKEY_STRINGS[i].c_str());

                    // we can only detect one key at a time
                    break;
                }

            } else {

                // forget old key press
                EXTDEV_CARDUNIT_TENKEY_STATE[unit][i] = false;
            }
        }
    }
}

static int __cdecl cardunit_get_status(int unit) {

    // might not be needed
    cardunit_update();

    // TODO: why only for reflec beat?
    if (avs::game::is_model("MBR")) {
        return EXTDEV_CARD_IN[unit] && is_card_uid_felica(EXTDEV_CARD[unit]) ? 2 : 1;
    }

    // gitadora always wants 1 apparently
    return 1;
}

static long __cdecl cardunit_cardnumber_obfuscate_decode(void* a1, int a2, void* a3, void* a4) {
    return 0;
}

static long __cdecl cardunit_cardnumber_obfuscate_encode(void* a1, int a2, void* a3, void* a4) {
    return 0;
}

static int __cdecl cardunit_get_errorcount(int a1) {
    return 0;
}

static int __cdecl cardunit_get_recvcount(int a1) {
    return 0;
}

static int __cdecl cardunit_get_sendcount(int a1) {
    return 0;
}

static const char* __cdecl cardunit_get_version(int a1) {
    static const char* ver = "DUMMY\x04\x02\x00";
    return ver;
}

static int __cdecl cardunit_check_version() {
    return 1;
}

static int __cdecl cardunit_key_get(int unit) {
    if (EXTDEV_CARDUNIT_KEY[unit].empty())
        return -1;
    return EXTDEV_CARDUNIT_KEY[unit].get();
}

static const char *__cdecl cardunit_key_str(int unit) {
    if (EXTDEV_CARDUNIT_KEY_STR[unit].empty())
        return nullptr;
    return EXTDEV_CARDUNIT_KEY_STR[unit].get();
}

static int __cdecl cardunit_reset() {
    return 0;
}

static void __cdecl cardunit_shutdown() {
}

static void __cdecl cardunit_sleep(int unit) {
}

static const size_t gitadora_button_mapping[] = {
        games::gitadora::Buttons::Service,
        games::gitadora::Buttons::Test,
        games::gitadora::Buttons::Coin,
        games::gitadora::Buttons::GuitarP1PickUp,
        games::gitadora::Buttons::GuitarP1PickDown,
        games::gitadora::Buttons::GuitarP1R,
        games::gitadora::Buttons::GuitarP1G,
        games::gitadora::Buttons::GuitarP1B,
        games::gitadora::Buttons::GuitarP1Y,
        games::gitadora::Buttons::GuitarP1P,
        games::gitadora::Buttons::GuitarP1KnobUp,
        games::gitadora::Buttons::GuitarP1KnobDown,
        games::gitadora::Buttons::GuitarP1WailUp,
        games::gitadora::Buttons::GuitarP1WailDown,
        games::gitadora::Buttons::GuitarP2PickUp,
        games::gitadora::Buttons::GuitarP2PickDown,
        games::gitadora::Buttons::GuitarP2R,
        games::gitadora::Buttons::GuitarP2G,
        games::gitadora::Buttons::GuitarP2B,
        games::gitadora::Buttons::GuitarP2Y,
        games::gitadora::Buttons::GuitarP2P,
        games::gitadora::Buttons::GuitarP2KnobUp,
        games::gitadora::Buttons::GuitarP2KnobDown,
        games::gitadora::Buttons::GuitarP2WailUp,
        games::gitadora::Buttons::GuitarP2WailDown,
        games::gitadora::Buttons::DrumHiHat,
        games::gitadora::Buttons::DrumHiHatClosed,
        games::gitadora::Buttons::DrumHiHatHalfOpen,
        games::gitadora::Buttons::DrumSnare,
        games::gitadora::Buttons::DrumHiTom,
        games::gitadora::Buttons::DrumLowTom,
        games::gitadora::Buttons::DrumRightCymbal,
        games::gitadora::Buttons::DrumBassPedal,
        games::gitadora::Buttons::DrumLeftCymbal,
        games::gitadora::Buttons::DrumLeftPedal,
        games::gitadora::Buttons::DrumFloorTom,
};

static const size_t gitadora_analog_mapping[] = {
        games::gitadora::Analogs::GuitarP1WailX,
        games::gitadora::Analogs::GuitarP1WailY,
        games::gitadora::Analogs::GuitarP1WailZ,
        games::gitadora::Analogs::GuitarP1Knob,
        games::gitadora::Analogs::GuitarP2WailX,
        games::gitadora::Analogs::GuitarP2WailY,
        games::gitadora::Analogs::GuitarP2WailZ,
        games::gitadora::Analogs::GuitarP2Knob,
};

static void __cdecl gfdm_unit_boot(unsigned int is_dm, unsigned int a2, unsigned int a3) {
}

static void __cdecl gfdm_unit2_boot(unsigned int is_dm, unsigned int a2, unsigned int a3) {
}

static int __cdecl gfdm_unit_boot_initialize() {
    return 1;
}

static int __cdecl gfdm_unit2_boot_initialize() {
    return 1;
}

constexpr int LEFTY_X_CENTER = 2040;

// this seems to be only used in the I/O test menu, not in gameplay
static void *__cdecl gfdm_unit_get_button_p(void *a1, int a2, size_t player) {
    memset(a1, 0, 64);

    // irrelevant for drummania
    if (games::gitadora::is_drum()) {
        return a1;
    }

    // log_info("gitadora", "gfdm_unit_get_button_p: a2={}, player={}", a2, player);

    // get buttons
    auto &buttons = games::gitadora::get_buttons();
    auto &analogs = games::gitadora::get_analogs();
    const auto wail_up =
        Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[12 + 11 * (size_t) player]));
    const auto wail_down =
        Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[13 + 11 * (size_t) player]));
    const auto wail_result =
        socd::get_guitar_wail(player, wail_up, wail_down, get_performance_milliseconds());

    // wail X
    ((int *) a1)[4] = games::gitadora::LEFTY ? LEFTY_X_CENTER : -4080;
    if (analogs.at(player * 3 + 0).isSet())
        ((int *) a1)[4] = lroundf(Analogs::getState(
                RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 0])) * 8160.f) - 4080;

    // wail Y
    ((int *) a1)[5] = 0;
    if (analogs.at(player * 3 + 1).isSet()) {
        ((int *) a1)[5] = lroundf(Analogs::getState(
                RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 1])) * 8160.f) - 4080;
    }

    // digital wailing
    if (!games::gitadora::LEFTY) {
        // righty
        if (wail_result == socd::TiltUp) {
            // top center
            ((int *) a1)[4] = 0; // X
            ((int *) a1)[5] = -4080; // Y
        } else if (wail_result == socd::TiltDown) {
            // bottom left; for lefty, bottom right
            ((int *) a1)[4] = -4080; // X
            ((int *) a1)[5] = 4080; // Y
        }
    } else {
        // lefty
        if (wail_result == socd::TiltUp) {
            // left
            ((int *) a1)[4] = -4080; // X
            ((int *) a1)[5] = 0; // Y
        } else if (wail_result == socd::TiltDown) {
            // right
            ((int *) a1)[4] = 4080; // X
            ((int *) a1)[5] = 0; // Y
        }   
    }

    // wail Z
    ((int *) a1)[6] = 0;
    if (analogs.at(player * 3 + 2).isSet())
        ((int *) a1)[6] = lroundf(Analogs::getState(
                RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 2])) * 8160.f) - 4080;

    // return the same buffer
    return a1;
}

static void *__cdecl gfdm_unit_get_button(void *a1, int a2) {
    return gfdm_unit_get_button_p(a1, a2, 0);
}

static void *__cdecl gfdm_unit2_get_button(void *a1, int a2) {
    return gfdm_unit_get_button_p(a1, a2, 1);
}

static bool __cdecl gfdm_unit_get_button_dm(int a1) {
    return a1 == 9 || a1 == 8;
}

static bool __cdecl gfdm_unit2_get_button_dm(int a1) {
    return a1 == 9 || a1 == 8;
}

static int __cdecl gfdm_unit_get_button_gf(int a1, int a2) {
    return 0;
}

static int __cdecl gfdm_unit2_get_button_gf(int a1, int a2) {
    return 0;
}

static long __cdecl gfdm_unit_get_dm_attack_border_value(int a1, int *a2) {
    if (a1 >= 7)
        return -1;
    memcpy(a2, GFDM_DM_ATTACK_BORDER_VALUE, 7 * sizeof(int32_t));
    return 0;
}

static long __cdecl gfdm_unit2_get_dm_attack_border_value(int a1, int *a2) {
    if (a1 >= 7)
        return -1;
    memcpy(a2, GFDM_DM_ATTACK_BORDER_VALUE, 7 * sizeof(int32_t));
    return 0;
}

static long __cdecl gfdm_unit_get_errorcount(int a1) {
    return 0;
}

static long __cdecl gfdm_unit2_get_errorcount(int a1) {
    return 0;
}

static long __cdecl gfdm_unit_get_input_p(int device, size_t player) {
    long ret = 0;

    // get buttons and analogs
    auto &buttons = games::gitadora::get_buttons();
    auto &analogs = games::gitadora::get_analogs();

    // drum mania
    if (games::gitadora::is_drum()) {

        // we don't want input for this
        if (device == 1)
            return 0;

        // hi hat
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[25])) ||
                Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[26])) ||
                Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[27])))
        {
            ret |= 0x20;
        }

        // snare
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[28]))) {
            ret |= 0x40;
        }

        // hi tom
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[29]))) {
            ret |= 0x80;
        }

        // low tom
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[30]))) {
            ret |= 0x100;
        }

        // right cymbal
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[31]))) {
            ret |= 0x200;
        }

        // bass pedal
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[32]))) {
            ret |= 0x800;
        }

        // left cymbal
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[33]))) {
            ret |= 0x4000;
        }

        // left pedal
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[34]))) {
            ret |= 0x8000;
        }

        // floor tom
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[35]))) {
            ret |= 0x10000;
        }
    }

    // guitar freaks
    if (games::gitadora::is_guitar()) {
        auto offset = player * 11;

        // pick up
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[3 + offset]))) {
            if (!GFDM_GF_PICK_STATE_UP[player]) {
                GFDM_GF_PICK_STATE_UP[player] = true;
                ret |= 0x80 | 0x20;
            }
        } else {
            GFDM_GF_PICK_STATE_UP[player] = false;
        }

        // pick down
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[4 + offset]))) {
            if (!GFDM_GF_PICK_STATE_DOWN[player]) {
                GFDM_GF_PICK_STATE_DOWN[player] = true;
                ret |= 0x100 | 0x20;
            }
        } else {
            GFDM_GF_PICK_STATE_DOWN[player] = false;
        }

        // button R
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[5 + offset]))) {
            ret |= 0x200;
        }

        // button G
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[6 + offset]))) {
            ret |= 0x400;
        }

        // button B
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[7 + offset]))) {
            ret |= 0x800;
        }

        // button Y
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[8 + offset]))) {
            ret |= 0x1000;
        }

        // button P
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[9 + offset]))) {
            ret |= 0x4000;
        }

        // knob statics
        static size_t knob[2]{};
        static long knob_flags[] = {
                0x8000,
                0x18000,
                0x10000,
                0x30000,
                0x38000,
                0x28000
        };

        // knob up
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[10 + offset]))) {
            if ((knob[player] >> 2) < 5) {
                knob[player]++;
            }
        }

        // knob down
        if (Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[11 + offset]))) {
            if (knob[player] > 0) {
                knob[player]--;
            }
        }

        // get value from 0 to 5
        size_t value = knob[player] >> 2;

        // analog override
        if (analogs.at(gitadora_analog_mapping[player * 4 + 3]).isSet()) {
            value = (size_t) (Analogs::getState(
                    RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 3])) * 5.999f);
        }

        // apply value
        ret |= knob_flags[value];
    }

    return ret;
}

static long __cdecl gfdm_unit_get_input(int device) {
    if (device > 0) {
        return gfdm_unit_get_input_p(device, 1);
    } else {
        return gfdm_unit_get_input_p(device, 0);
    }
}

static long __cdecl gfdm_unit2_get_input(int device) {
    return gfdm_unit_get_input_p(device, 1);
}

// this is used in gameplay
static long __cdecl gfdm_unit_get_sensor_gf_p(int a1, int a2, size_t player) {

    // return if it's actually drum mania
    if (games::gitadora::is_drum())
        return 0;

    // log_info("gitadora", "gfdm_unit_get_sensor_gf_p: a1={}, a2={}, player={}", a1, a2, player);

    // get buttons and analogs
    auto &buttons = games::gitadora::get_buttons();
    auto &analogs = games::gitadora::get_analogs();

    // figure out digital wail Y
    const size_t offset = (size_t) player * 11;
    const auto wail_up = Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[12 + offset]));
    const auto wail_down = Buttons::getState(RI_MGR, buttons.at(gitadora_button_mapping[13 + offset]));
    const auto wail_result = socd::get_guitar_wail(player, wail_up, wail_down, get_performance_milliseconds());

    // wail X
    if (a2 == 0) {
        long ret = games::gitadora::LEFTY ? LEFTY_X_CENTER : -4080;

        // analog override
        if (analogs.at(gitadora_analog_mapping[player * 3 + 0]).isSet()) {
            ret = lroundf(Analogs::getState(
                    RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 0])) * 8160.f) - 4080;
        }

        // digital wail up/down
        if (wail_result == socd::TiltUp) {
            ret = games::gitadora::LEFTY ? -4080 : 0;
        } else if (wail_result == socd::TiltDown) {
            ret = games::gitadora::LEFTY ? +4080 : -4080;
        }

        // default
        return ret;
    }

    // wail Y
    if (a2 == 1) {
        long ret = 0;

        // analog override
        if (analogs.at(player * 3 + 1).isSet()) {
            ret = lroundf(Analogs::getState(
                    RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 1])) * 8160.f) - 4080;
        }

        // digital wail up/down
        if (wail_result == socd::TiltUp) {
            ret = games::gitadora::LEFTY ? 0 : -4080;
        } else if (wail_result == socd::TiltDown) {
            ret = games::gitadora::LEFTY ? 0 : 4080;
        }

        // return value
        return ret;
    }

    // wail Z
    if (a2 == 2) {

        // analog override
        if (analogs.at(gitadora_analog_mapping[player * 3 + 2]).isSet()) {
            return lroundf(Analogs::getState(
                    RI_MGR, analogs.at(gitadora_analog_mapping[player * 4 + 2])) * 8160.f) - 4080;
        }

        // default
        return 0;
    }

    // unknown sensor
    return 0;
}

static long __cdecl gfdm_unit_get_sensor_gf(int a1, int a2) {
    return gfdm_unit_get_sensor_gf_p(a1, a2, 0);
}

static long __cdecl gfdm_unit2_get_sensor_gf(int a1, int a2) {
    return gfdm_unit_get_sensor_gf_p(a1, a2, 1);
}

static int __cdecl gfdm_unit_get_status(int a1) {
    return 1;
}

static int __cdecl gfdm_unit2_get_status(int a1) {
    return 1;
}

static int __cdecl gfdm_unit_get_stream_errorcount(int a1) {
    return 0;
}

static int __cdecl gfdm_unit2_get_stream_errorcount(int a1) {
    return 0;
}

static const char *__cdecl gfdm_unit_get_version() {
    static const char* ver = "DUMMY\x04\x02\x00";
    return ver;
}

static const char *__cdecl gfdm_unit2_get_version() {
    static const char* ver = "DUMMY\x04\x02\x00";
    return ver;
}

static long __cdecl gfdm_unit_reset() {
    return 0;
}

static long __cdecl gfdm_unit2_reset() {
    return 0;
}

static void __cdecl gfdm_unit_reset_stream_errorcount(int a1) {
}

static void __cdecl gfdm_unit2_reset_stream_errorcount(int a1) {
}

static int __cdecl gfdm_unit_send_motor_value(size_t motor, uint8_t value) {
    if (motor == 0) {
        auto &lights = games::gitadora::get_lights();
        auto &light = lights.at(games::gitadora::Lights::GuitarP1Motor);
        GameAPI::Lights::writeLight(RI_MGR, light, value / 127.f);
    }
    return 0;
}

static int __cdecl gfdm_unit2_send_motor_value(size_t motor, uint8_t value) {
    if (motor == 0) {
        auto &lights = games::gitadora::get_lights();
        auto &light = lights.at(games::gitadora::Lights::GuitarP2Motor);
        GameAPI::Lights::writeLight(RI_MGR, light, value / 127.f);
    }
    return 0;
}

static void __cdecl gfdm_unit_set_dm_attack_border_value(int32_t *a1) {
    memcpy(GFDM_DM_ATTACK_BORDER_VALUE, a1, 7 * sizeof(int32_t));
}

static void __cdecl gfdm_unit2_set_dm_attack_border_value(int32_t *a1) {
    memcpy(GFDM_DM_ATTACK_BORDER_VALUE, a1, 7 * sizeof(int32_t));
}

static void __cdecl gfdm_unit_shutdown() {
}

static void __cdecl gfdm_unit2_shutdown() {
}

static void __cdecl gfdm_unit_update() {
}

static void __cdecl gfdm_unit2_update() {
}

static const char* __cdecl led_get_version() {
    static const char* ver = "DUMMY\x04\x02\x00";
    return ver;
}

static void __cdecl led_unit_boot(int a1, int a2) {
}

static int __cdecl led_unit_boot_initialize() {
    return 0;
}

static int __cdecl led_unit_get_errorcount(int a1) {
    return 0;
}

static int __cdecl led_unit_get_status() {
    return 1;
}

static int __cdecl led_unit_reset() {
    return 0;
}

static void set_led_state(games::gitadora::Lights::gitadora_lights_t light, bool led_on) {
    auto &lights = games::gitadora::get_lights();
    GameAPI::Lights::writeLight(RI_MGR, lights.at(light), (led_on ? 1.f : 0.f));
}

static void set_led_value(games::gitadora::Lights::gitadora_lights_t light, uint8_t value) {
    auto &lights = games::gitadora::get_lights();
    GameAPI::Lights::writeLight(RI_MGR, lights.at(light), value / 128.f);
}

static unsigned int __cdecl led_unit_send_custom1(int index, uint8_t *data) {
    return 0;
}

struct ExtDevLight {
    int data_index;
    games::gitadora::Lights::gitadora_lights_t light;
    ExtDevLight(
        int data_index, games::gitadora::Lights::gitadora_lights_t light) :
        data_index(data_index), light(light) {}
};

// GITADORA series
static unsigned int __cdecl led_unit_send_custom2(int index, uint8_t *data) {
#ifdef SPICE64
    return gitadora_led_common(index, data);
#else
    return 0;
#endif
}

// XG series
static long __cdecl led_unit_send_direct(long index, uint8_t *data) {
#ifdef SPICE64
    return 0;
#else
    return gitadora_led_common(index, data);
#endif
}

static long __cdecl gitadora_led_common(long index, uint8_t *data) {
    // log_info("extdev::led", "gitadora_led_common {}", index);
    // for (size_t i = 0; i < 24; i += 3) {
    //     log_info("extdev::led", "gitadora_led_common ... {} {} {}", data[i], data[i+1], data[i+2]);
    // }

    if (games::gitadora::is_drum()) {
        // Drum: index is always 0, data array has 24 elements, each value ranging [0, 128]
        static ExtDevLight mapping_dm[] = {
            {0, games::gitadora::Lights::DrumWooferR},
            {1, games::gitadora::Lights::DrumWooferG},
            {2, games::gitadora::Lights::DrumWooferB},

            {3, games::gitadora::Lights::DrumSpotRearLeft},
            {7, games::gitadora::Lights::DrumSpotRearRight},
            {11, games::gitadora::Lights::SpotFrontLeft},

            {12, games::gitadora::Lights::DrumStageR},
            {13, games::gitadora::Lights::DrumStageG},
            {14, games::gitadora::Lights::DrumStageB},

            {15, games::gitadora::Lights::SpotCenterLeft},
            {19, games::gitadora::Lights::SpotCenterRight},
            {23, games::gitadora::Lights::SpotFrontRight}
        };
        if (index == 0) {
            for (const auto &map : mapping_dm) {
                set_led_value(map.light, data[map.data_index]);
            }
        }
    } else {
        // Guitar: index is {0,1,2}, data array has 24 elements, each value ranging [0, 128]
        static ExtDevLight mapping0[] = {
            {0, games::gitadora::Lights::GuitarLowerLeftR},
            {1, games::gitadora::Lights::GuitarLowerLeftG},
            {2, games::gitadora::Lights::GuitarLowerLeftB},

            {3, games::gitadora::Lights::SpotFrontLeft},
            {7, games::gitadora::Lights::SpotCenterLeft},

            {8, games::gitadora::Lights::GuitarLowerRightR},
            {9, games::gitadora::Lights::GuitarLowerRightG},
            {10, games::gitadora::Lights::GuitarLowerRightB},

            {11, games::gitadora::Lights::SpotCenterRight},
            {15, games::gitadora::Lights::SpotFrontRight}
        };

        static ExtDevLight mapping1[] = {
            {0, games::gitadora::Lights::GuitarLeftSpeakerUpperR},
            {1, games::gitadora::Lights::GuitarLeftSpeakerUpperG},
            {2, games::gitadora::Lights::GuitarLeftSpeakerUpperB},

            {4, games::gitadora::Lights::GuitarLeftSpeakerMidUpLeftR},
            {5, games::gitadora::Lights::GuitarLeftSpeakerMidUpLeftG},
            {6, games::gitadora::Lights::GuitarLeftSpeakerMidUpLeftB},

            {8, games::gitadora::Lights::GuitarLeftSpeakerMidUpRightR},
            {9, games::gitadora::Lights::GuitarLeftSpeakerMidUpRightG},
            {10, games::gitadora::Lights::GuitarLeftSpeakerMidUpRightB},

            {12, games::gitadora::Lights::GuitarLeftSpeakerMidLowLeftR},
            {13, games::gitadora::Lights::GuitarLeftSpeakerMidLowLeftG},
            {14, games::gitadora::Lights::GuitarLeftSpeakerMidLowLeftB},

            {16, games::gitadora::Lights::GuitarLeftSpeakerMidLowRightR},
            {17, games::gitadora::Lights::GuitarLeftSpeakerMidLowRightG},
            {18, games::gitadora::Lights::GuitarLeftSpeakerMidLowRightB},

            {20, games::gitadora::Lights::GuitarLeftSpeakerLowerR},
            {21, games::gitadora::Lights::GuitarLeftSpeakerLowerG},
            {22, games::gitadora::Lights::GuitarLeftSpeakerLowerB}
        };

        static ExtDevLight mapping2[] = {
            {0, games::gitadora::Lights::GuitarRightSpeakerUpperR},
            {1, games::gitadora::Lights::GuitarRightSpeakerUpperG},
            {2, games::gitadora::Lights::GuitarRightSpeakerUpperB},

            {4, games::gitadora::Lights::GuitarRightSpeakerMidUpLeftR},
            {5, games::gitadora::Lights::GuitarRightSpeakerMidUpLeftG},
            {6, games::gitadora::Lights::GuitarRightSpeakerMidUpLeftB},

            {8, games::gitadora::Lights::GuitarRightSpeakerMidUpRightR},
            {9, games::gitadora::Lights::GuitarRightSpeakerMidUpRightG},
            {10, games::gitadora::Lights::GuitarRightSpeakerMidUpRightB},

            {12, games::gitadora::Lights::GuitarRightSpeakerMidLowLeftR},
            {13, games::gitadora::Lights::GuitarRightSpeakerMidLowLeftG},
            {14, games::gitadora::Lights::GuitarRightSpeakerMidLowLeftB},

            {16, games::gitadora::Lights::GuitarRightSpeakerMidLowRightR},
            {17, games::gitadora::Lights::GuitarRightSpeakerMidLowRightG},
            {18, games::gitadora::Lights::GuitarRightSpeakerMidLowRightB},

            {20, games::gitadora::Lights::GuitarRightSpeakerLowerR},
            {21, games::gitadora::Lights::GuitarRightSpeakerLowerG},
            {22, games::gitadora::Lights::GuitarRightSpeakerLowerB}
        };

        switch (index) {
            case 0:
                for (const auto &map : mapping0) {
                    set_led_value(map.light, data[map.data_index]);
                }
                break;
            case 1:
                for (const auto &map : mapping1) {
                    set_led_value(map.light, data[map.data_index]);
                }
                break;
            case 2:
                for (const auto &map : mapping2) {
                    set_led_value(map.light, data[map.data_index]);
                }
                break;
            default:
                break;
        }
    }
    return 0;
}

static void __cdecl led_unit_shutdown() {
}

static void __cdecl led_unit_update() {
}

static int __cdecl sci_boot() {
    return 1;
}

static int __cdecl sci_clear_error(int a1) {
    return 0;
}

static int __cdecl sci_close(int a1) {
    return 0;
}

static int __cdecl sci_flush() {
    return 0;
}

static int __cdecl sci_flush_complete() {
    return 1;
}

static int __cdecl sci_get_error(int a1) {
    return 0;
}

static int __cdecl sci_gets(int a1, int a2, int a3) {
    return 0;
}

static int __cdecl sci_print_error(int a1, void *a2) {
    return 0;
}

static int __cdecl sci_puts(int a1, int a2, int a3) {
    return 0;
}

static int __cdecl sci_set_linebreak(int a1, int a2) {
    return 0;
}

static int __cdecl sci_setparam(int a1, int a2, int a3, char a4) {
    return 1;
}

static void __cdecl dm_output_set_padled(LED_INDEX index, bool led_on) {
    games::gitadora::Lights::gitadora_lights_t lights[] = {
        games::gitadora::Lights::DrumHighTom,
        games::gitadora::Lights::DrumLowTom,
        games::gitadora::Lights::DrumSnare,
        games::gitadora::Lights::DrumFloorTom,
        games::gitadora::Lights::DrumLeftCymbal,
        games::gitadora::Lights::DrumRightCymbal,
        games::gitadora::Lights::DrumHiHat
    };

    // log_info("gitadora", "dm_output_set_padled {} {}", index, led_on);

    if (index < std::size(lights)) {
        set_led_state(lights[index], led_on);
    }
}

static void __cdecl dm_output_set_startled(bool led_on) {
    set_led_state(games::gitadora::Lights::P1MenuStart, led_on);
}

static void __cdecl dm_output_set_up_down(bool led_on) {
    set_led_state(games::gitadora::Lights::P1MenuUpDown, led_on);
}

static void __cdecl dm_output_set_left_right(bool led_on) {
    set_led_state(games::gitadora::Lights::P1MenuLeftRight, led_on);
}

static void __cdecl dm_output_set_help(bool led_on) {
    set_led_state(games::gitadora::Lights::P1MenuHelp, led_on);
}

static void __cdecl gf_output_set_startlamp(LED_INDEX index, bool led_on) {
    if (index == 0) {
        set_led_state(games::gitadora::Lights::P1MenuStart, led_on);
    } else if (index == 1) {
        set_led_state(games::gitadora::Lights::P2MenuStart, led_on);
    }
}

static void __cdecl gf_output_set_up_down(LED_INDEX index, bool led_on) {
    if (index == 0) {
        set_led_state(games::gitadora::Lights::P1MenuUpDown, led_on);
    } else if (index == 1) {
        set_led_state(games::gitadora::Lights::P2MenuUpDown, led_on);
    }
}

static void __cdecl gf_output_set_left_right(LED_INDEX index, bool led_on) {
    if (index == 0) {
        set_led_state(games::gitadora::Lights::P1MenuLeftRight, led_on);
    } else if (index == 1) {
        set_led_state(games::gitadora::Lights::P2MenuLeftRight, led_on);
    }
}

static void __cdecl gf_output_set_help(LED_INDEX index, bool led_on) {
    if (index == 0) {
        set_led_state(games::gitadora::Lights::P1MenuHelp, led_on);
    } else if (index == 1) {
        set_led_state(games::gitadora::Lights::P2MenuHelp, led_on);
    }
}

void extdev_attach() {

    // get instance
    for (auto &name : EXTDEV_INSTANCE_NAMES) {
        auto instance = libutils::try_module(name);
        if (instance) {
            EXTDEV_INSTANCE = instance;
        } else {
            continue;
        }

        // card unit
        detour::inline_hook((void *) cardunit_boot2, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_boot2", "?cardunit_boot2@@YAXHHH@Z"}));
        detour::inline_hook((void *) cardunit_boot, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_boot", "?cardunit_boot@@YAXHH@Z"}));
        detour::inline_hook((void *) cardunit_boot_initialize, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_boot_initialize", "?cardunit_boot_initialize@@YAHXZ"}));
        detour::inline_hook((void *) cardunit_boot_no_slot_type, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_boot_no_slot_type", "?cardunit_boot_no_slot_type@@YAXHH@Z"}));
        detour::inline_hook((void *) cardunit_card_eject, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_eject", "?cardunit_card_eject@@YAXH@Z"}));
        detour::inline_hook((void *) cardunit_card_eject_complete, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_eject_complete", "?cardunit_card_eject_complete@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_card_eject_wait, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_eject_wait", "?cardunit_card_eject_wait@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_card_read2, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_read2",
                                  "?cardunit_card_read2@@YAHHQAEPAH@Z",
                                  "?cardunit_card_read2@@YAHHQEAEPEAH@Z"}));
        detour::inline_hook((void *) cardunit_card_read, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_read", "?cardunit_card_read@@YAHHQAE@Z"}));
        detour::inline_hook((void *) cardunit_card_ready, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_ready", "?cardunit_card_ready@@YAXH@Z"}));
        detour::inline_hook((void *) cardunit_card_sensor, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_sensor", "?cardunit_card_sensor@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_card_sensor_raw, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_card_sensor_raw", "?cardunit_card_sensor_raw@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_update, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_update", "?cardunit_update@@YAXXZ"}));
        detour::inline_hook((void *) cardunit_get_status, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_get_status", "?cardunit_get_status@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_cardnumber_obfuscate_decode, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_cardnumber_obfuscate_decode",
                                  "?cardunit_cardnumber_obfuscate_decode@@YAHPEADHPEBD1@Z"}));
        detour::inline_hook((void *) cardunit_cardnumber_obfuscate_encode, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_cardnumber_obfuscate_encode",
                                  "?cardunit_cardnumber_obfuscate_encode@@YAHPEADHPEBD1@Z"}));
        detour::inline_hook((void *) cardunit_get_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_get_errorcount", "?cardunit_get_errorcount@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_get_recvcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_get_recvcount", "?cardunit_get_recvcount@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_get_sendcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_get_sendcount", "?cardunit_get_sendcount@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_get_version, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_get_version",
                                  "?cardunit_get_version@@YAPBUcardunit_firm_version@@H@Z"}));
        detour::inline_hook((void *) cardunit_check_version, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_check_version", "?cardunit_check_version@@YAEH@Z"}));
        detour::inline_hook((void *) cardunit_key_get, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_key_get", "?cardunit_key_get@@YAHH@Z"}));
        detour::inline_hook((void *) cardunit_key_str, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_key_str", "?cardunit_key_str@@YAPBDH@Z"}));
        detour::inline_hook((void *) cardunit_reset, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_reset", "?cardunit_reset@@YAHXZ"}));
        detour::inline_hook((void *) cardunit_shutdown, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_shutdown", "?cardunit_shutdown@@YAXXZ"}));
        detour::inline_hook((void *) cardunit_sleep, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"cardunit_sleep", "?cardunit_sleep@@YAXH@Z"}));

        // GFDM UNIT
        detour::inline_hook((void *) gfdm_unit_boot, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_boot",
                                  "?gfdm_unit_boot@@YAXW4UNIT_TYPE@@HH@Z"}));
        detour::inline_hook((void *) gfdm_unit_boot_initialize, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_boot_initialize",
                                  "?gfdm_unit_boot_initialize@@YAHXZ"}));
        detour::inline_hook((void *) gfdm_unit_get_button, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_button",
                                  "?gfdm_unit_get_button@@YA?AUunit_button_t@@H@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_button_dm, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_button_dm",
                                  "?gfdm_unit_get_button_dm@@YAHW4DM_BUTTON_DEFINITION@@@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_button_gf, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_button_gf",
                                  "?gfdm_unit_get_button_gf@@YAHW4UNIT_NO@@W4GF_BUTTON_DEFINITION@@@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_dm_attack_border_value, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_dm_attack_border_value",
                                  "?gfdm_unit_get_dm_attack_border_value@@YAHHPEAH@Z",
                                  "?gfdm_unit_get_dm_attack_border_value@@YAHHPAH@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_errorcount",
                                  "?gfdm_unit_get_errorcount@@YAHH@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_input, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_input",
                                  "?gfdm_unit_get_input@@YAIH@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_sensor_gf, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_sensor_gf",
                                  "?gfdm_unit_get_sensor_gf@@YAHW4UNIT_NO@@W4GF_SENSOR_DEFINITION@@@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_status, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_status",
                                  "?gfdm_unit_get_status@@YAHH@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_stream_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_stream_errorcount",
                                  "?gfdm_unit_get_stream_errorcount@@YAHH@Z"}));
        detour::inline_hook((void *) gfdm_unit_get_version, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_get_version",
                                  "?gfdm_unit_get_version@@YAPEBUfirm_version@@H@Z",
                                  "?gfdm_unit_get_version@@YAPBUfirm_version@@H@Z"}));
        detour::inline_hook((void *) gfdm_unit_reset, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_reset",
                                  "?gfdm_unit_reset@@YAHXZ"}));
        detour::inline_hook((void *) gfdm_unit_reset_stream_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_reset_stream_errorcount",
                                  "?gfdm_unit_reset_stream_errorcount@@YAXH@Z"}));
        detour::inline_hook((void *) gfdm_unit_send_motor_value, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_send_motor_value",
                                  "?gfdm_unit_send_motor_value@@YAIHE@Z"}));
        detour::inline_hook((void *) gfdm_unit_set_dm_attack_border_value, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_set_dm_attack_border_value",
                                  "?gfdm_unit_set_dm_attack_border_value@@YAXQEAH@Z",
                                  "?gfdm_unit_set_dm_attack_border_value@@YAXQAH@Z"}));
        detour::inline_hook((void *) gfdm_unit_shutdown, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_shutdown",
                                  "?gfdm_unit_shutdown@@YAXXZ"}));
        detour::inline_hook((void *) gfdm_unit_update, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit_update",
                                  "?gfdm_unit_update@@YAXXZ"}));

        // GFDM UNIT 2
        detour::inline_hook((void *) gfdm_unit2_boot, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_boot",
                                  "?gfdm_unit2_boot@@YAXW4UNIT_TYPE@@HH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_boot_initialize, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_boot_initialize",
                                  "?gfdm_unit2_boot_initialize@@YAHXZ"}));
        detour::inline_hook((void *) gfdm_unit2_get_button, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_button",
                                  "?gfdm_unit2_get_button@@YA?AUunit_button_t@@H@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_button_dm, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_button_dm",
                                  "?gfdm_unit2_get_button_dm@@YAHW4DM_BUTTON_DEFINITION@@@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_button_gf, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_button_gf",
                                  "?gfdm_unit2_get_button_gf@@YAHW4UNIT_NO@@W4GF_BUTTON_DEFINITION@@@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_dm_attack_border_value, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_dm_attack_border_value",
                                  "?gfdm_unit2_get_dm_attack_border_value@@YAHHPEAH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_errorcount",
                                  "?gfdm_unit2_get_errorcount@@YAHH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_input, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_input",
                                  "?gfdm_unit2_get_input@@YAIH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_sensor_gf, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_sensor_gf",
                                  "?gfdm_unit2_get_sensor_gf@@YAHW4UNIT_NO@@W4GF_SENSOR_DEFINITION@@@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_status, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_status",
                                  "?gfdm_unit2_get_status@@YAHH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_stream_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_stream_errorcount",
                                  "?gfdm_unit2_get_stream_errorcount@@YAHH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_get_version, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_get_version",
                                  "?gfdm_unit2_get_version@@YAPEBUfirm_version@@H@Z",
                                  "?gfdm_unit2_get_version@@YAPBUfirm_version@@H@Z"}));
        detour::inline_hook((void *) gfdm_unit2_reset, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_reset",
                                  "?gfdm_unit2_reset@@YAHXZ"}));
        detour::inline_hook((void *) gfdm_unit2_reset_stream_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_reset_stream_errorcount",
                                  "?gfdm_unit2_reset_stream_errorcount@@YAXH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_send_motor_value, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_send_motor_value",
                                  "?gfdm_unit2_send_motor_value@@YAIHE@Z"}));
        detour::inline_hook((void *) gfdm_unit2_set_dm_attack_border_value, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_set_dm_attack_border_value",
                                  "?gfdm_unit2_set_dm_attack_border_value@@YAXQEAH@Z"}));
        detour::inline_hook((void *) gfdm_unit2_shutdown, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_shutdown",
                                  "?gfdm_unit2_shutdown@@YAXXZ"}));
        detour::inline_hook((void *) gfdm_unit2_update, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"gfdm_unit2_update",
                                  "?gfdm_unit2_update@@YAXXZ"}));

        // led unit
        detour::inline_hook((void *) led_get_version, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_get_version",
                                  "?led_get_version@@YAPEBUfirm_version@@H@Z",
                                  "?led_get_version@@YAPBUfirm_version@@H@Z"}));
        detour::inline_hook((void *) led_unit_boot, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_boot",
                                  "?led_unit_boot@@YAXHH@Z"}));
        detour::inline_hook((void *) led_unit_boot_initialize, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_boot_initialize",
                                  "?led_unit_boot_initialize@@YAHXZ"}));
        detour::inline_hook((void *) led_unit_get_errorcount, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_get_errorcount",
                                  "?led_unit_get_errorcount@@YAHH@Z"}));
        detour::inline_hook((void *) led_unit_get_status, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_get_status",
                                  "?led_unit_get_status@@YAHH@Z"}));
        detour::inline_hook((void *) led_unit_reset, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_reset",
                                  "?led_unit_reset@@YAHXZ"}));
        detour::inline_hook((void *) led_unit_send_custom1, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_send_custom1",
                                  "?led_unit_send_custom1@@YAIHQEAE@Z",
                                  "?led_unit_send_custom1@@YAIHQAE@Z"}));
        detour::inline_hook((void *) led_unit_send_custom2, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_send_custom2",
                                  "?led_unit_send_custom2@@YAIHQEAE@Z",
                                  "?led_unit_send_custom2@@YAIHQAE@Z"}));
        detour::inline_hook((void *) led_unit_send_direct, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_send_direct",
                                  "?led_unit_send_direct@@YAIHQEAE@Z",
                                  "?led_unit_send_direct@@YAIHQAE@Z"}));
        detour::inline_hook((void *) led_unit_shutdown, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_shutdown",
                                  "?led_unit_shutdown@@YAXXZ"}));
        detour::inline_hook((void *) led_unit_update, libutils::try_proc_list(
                EXTDEV_INSTANCE, {"led_unit_update",
                                  "?led_unit_update@@YAXXZ"}));

        // SCI
        detour::inline_hook((void *) sci_boot, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_boot"));
        detour::inline_hook((void *) sci_clear_error, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_clear_error"));
        detour::inline_hook((void *) sci_close, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_close"));
        detour::inline_hook((void *) sci_flush, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_flush"));
        detour::inline_hook((void *) sci_flush_complete, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_flush_complete"));
        detour::inline_hook((void *) sci_get_error, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_get_error"));
        detour::inline_hook((void *) sci_gets, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_gets"));
        detour::inline_hook((void *) sci_print_error, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_print_error"));
        detour::inline_hook((void *) sci_puts, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_puts"));
        detour::inline_hook((void *) sci_set_linebreak, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_set_linebreak"));
        detour::inline_hook((void *) sci_setparam, libutils::try_proc(
                EXTDEV_INSTANCE, "sci_setparam"));

        // libshare-pj.dll - dm_output
        detour::inline_hook((void *) dm_output_set_padled, libutils::try_proc(
                EXTDEV_INSTANCE, "dm_output_set_padled"));
        detour::inline_hook((void *) dm_output_set_startled, libutils::try_proc(
                EXTDEV_INSTANCE, "dm_output_set_startled"));
        detour::inline_hook((void *) dm_output_set_up_down, libutils::try_proc(
                EXTDEV_INSTANCE, "dm_output_set_up_down"));
        detour::inline_hook((void *) dm_output_set_left_right, libutils::try_proc(
                EXTDEV_INSTANCE, "dm_output_set_left_right"));
        detour::inline_hook((void *) dm_output_set_help, libutils::try_proc(
                EXTDEV_INSTANCE, "dm_output_set_help"));

        // libshare-pj.dll - gf_output
        detour::inline_hook((void *) gf_output_set_startlamp, libutils::try_proc(
                EXTDEV_INSTANCE, "gf_output_set_startlamp"));
        detour::inline_hook((void *) gf_output_set_up_down, libutils::try_proc(
                EXTDEV_INSTANCE, "gf_output_set_up_down"));
        detour::inline_hook((void *) gf_output_set_left_right, libutils::try_proc(
                EXTDEV_INSTANCE, "gf_output_set_left_right"));
        detour::inline_hook((void *) gf_output_set_help, libutils::try_proc(
                EXTDEV_INSTANCE, "gf_output_set_help"));
    }
}

void extdev_detach() {
    // TODO
}
