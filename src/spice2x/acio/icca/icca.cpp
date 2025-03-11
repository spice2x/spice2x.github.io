#include <cmath>
#include "icca.h"

#include "avs/game.h"
#include "misc/eamuse.h"
#include "util/time.h"

// settings
namespace acio {
    bool ICCA_FLIP_ROWS = false;
    bool ICCA_COMPAT_ACTIVE = false;
}

/*
 * Helpers
 */

struct ICCA_STATUS {
    uint8_t status_code;
    uint8_t solenoid;
    uint8_t front_sensor;
    uint8_t rear_sensor;
    uint8_t uid[8];
    int32_t error;
    uint32_t key_edge;
    uint32_t key_level;
};
struct ICCA_STATUS_LA9 {
    uint8_t status_code;
    uint8_t card_in;
    uint8_t uid[8];
    uint8_t error;
    uint8_t uid2[8];
};
static_assert(sizeof(struct ICCA_STATUS) == 24, "ICCA_STATUS must be 24 bytes");

enum ICCA_WORKFLOW {
    STEP,
    SLEEP,
    START,
    INIT,
    READY,
    GET_USERID,
    ACTIVE,
    EJECT,
    EJECT_CHECK,
    END,
    CLOSE_EJECT,
    CLOSE_E_CHK,
    CLOSE_END,
    ERR_GETUID = -2
};
struct ICCA_UNIT {
    struct ICCA_STATUS status {};
    enum ICCA_WORKFLOW state = STEP;
    bool card_cmd_pressed = false;
    bool card_in = false;
    double card_in_time = 0.0;
    char key_serial = 0;
    bool uid_skip = false;
    bool initialized = false;
    int felica_retries = 0;
};
static ICCA_UNIT ICCA_UNITS[2] {};
static bool IS_LAST_CARD_FELICA = false;
static bool STATUS_BUFFER_FREEZE = false;
static double CARD_TIMEOUT = 2.0;

static inline int icca_get_active_count() {
    int active_count = 0;
    for (auto unit : ICCA_UNITS) {
        active_count += unit.initialized;
    }
    return active_count;
}

static inline int icca_get_unit_id(int unit_id) {
    if (icca_get_active_count() < 2)
        return 1;
    else {
        if (unit_id > 1) {
            return 1;
        } else {
            return 0;
        }
    }
}

static inline void update_card(int unit_id) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return;
    }

    // eamio keypress
    int index = unit_id > 0 && icca_get_active_count() > 1 ? 1 : 0;
    bool kb_insert_press = (eamuse_get_keypad_state(index) & (1 << EAM_IO_INSERT)) > 0;
    static bool kb_insert_press_old[2] = {false, false};

    // get unit
    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];

    const bool card_presented = eamuse_card_insert_consume(icca_get_active_count(), unit_id);
    const bool key_pressed = (kb_insert_press && !kb_insert_press_old[unit_id]);

    // beatstream and nostalgia have logic that requires ac_io_icca_get_uid_felica to return the
    // exact same card number multiple times in a row in order for the card number to be read...
    // for whatever reason setting this to 3-4 doesn't work the very first time the game boots up
    // so we use 10 just to be safe
    const bool need_felica_retries = avs::game::is_model({"NBT", "PAN"});
    if (need_felica_retries && (card_presented || key_pressed)) {
        unit->felica_retries = 10;
    }

    // check for card insert
    if (card_presented || key_pressed || (0 < unit->felica_retries)) {
        if (!unit->card_cmd_pressed) {
            unit->card_cmd_pressed = true;
            if (unit->state == GET_USERID || unit->state == CLOSE_EJECT || unit->state == STEP) {
                if (unit->uid_skip || eamuse_get_card(icca_get_active_count(), unit_id, unit->status.uid)) {
                    IS_LAST_CARD_FELICA = is_card_uid_felica(unit->status.uid);

                    unit->state = acio::ICCA_COMPAT_ACTIVE ? START : ACTIVE;
                    unit->status.error = 0;
                } else {
                    unit->state = ERR_GETUID;
                    memset(unit->status.uid, 0, 8);
                }
                unit->card_in = true;
                unit->card_in_time = get_performance_seconds();
            } else if (unit->state == EJECT_CHECK) {
                unit->state = SLEEP;
                unit->card_in = false;
            }
        } else {
            unit->state = acio::ICCA_COMPAT_ACTIVE ? START : ACTIVE;
            unit->status.error = 0;
            unit->card_in = true;
            unit->card_in_time = get_performance_seconds();
        }
    } else {
        unit->card_cmd_pressed = false;
        unit->state = CLOSE_EJECT;
        if (fabs(get_performance_seconds() - unit->card_in_time) > CARD_TIMEOUT) {
            unit->card_in = false;
        }
    }

    // save state
    kb_insert_press_old[unit_id] = kb_insert_press;
}

static bool KEYPAD_LAST[2][12];
static uint32_t KEYPAD_EAMUSE_MAPPING[] = {
        0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4
};
static uint32_t KEYPAD_KEY_CODES[] = {
        0x100,
        0x200,
        0x2000,
        2,
        0x400,
        0x4000,
        4,
        0x800,
        0x8000,
        8,
        1,
        0x1000
};
static uint32_t KEYPAD_KEY_CODE_NUMS[] = {
        0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4
};

static inline void keypad_update(int unit_id) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return;
    }

    // reset unit
    struct ICCA_UNIT *unit = &ICCA_UNITS[unit_id];
    unit->status.key_level = 0;
    unit->status.error = 0;

    // get eamu state
    int index = unit_id > 0 && icca_get_active_count() > 1 ? 1 : 0;
    uint16_t eamu_state = eamuse_get_keypad_state(index);

    // iterate keypad
    for (int n = 0; n < 12; n++) {
        int i = n;

        // flip 123 with 789
        if (acio::ICCA_FLIP_ROWS) {
            if (!n)
                i = 11;
            else if (n < 4)
                i = n + 6;
            else if (n > 6 && n < 10)
                i = n - 6;
            else if (n == 11)
                i = 0;
        }

        // check if pressed
        if ((eamu_state & (1 << KEYPAD_EAMUSE_MAPPING[i])))
        {
            unit->status.key_level |= KEYPAD_KEY_CODES[i];

            if (!KEYPAD_LAST[unit_id][i]) {
                unit->status.key_edge = KEYPAD_KEY_CODES[n] << 16;
                unit->status.key_edge |= 0x80 | (unit->key_serial << 4) | KEYPAD_KEY_CODE_NUMS[n];

                unit->key_serial += 1;
                unit->key_serial &= 0x07;
            }
            KEYPAD_LAST[unit_id][i] = true;
        } else {
            unit->status.key_edge &= ~(KEYPAD_KEY_CODES[n] << 16);

            KEYPAD_LAST[unit_id][i] = false;
        }
    }
}

/*
 * Implementations
 */

static bool __cdecl ac_io_icca_cardunit_init(int unit_id) {
    unit_id = icca_get_unit_id(unit_id);

    // dirty workaround code
    if (icca_get_active_count() < 1)
        ICCA_UNITS[unit_id].initialized = true;
    else {
        ICCA_UNITS[0].initialized = true;
        ICCA_UNITS[1].initialized = true;
    }

    // initial poll
    eamuse_get_keypad_state(unit_id);

    // return success
    return true;
}

static char __cdecl ac_io_icca_cardunit_init_isfinished(int unit_id, DWORD *status) {
    *status = READY;

    return 1;
}

static char __cdecl ac_io_icca_crypt_init(int unit_id) {
    return 1;
}

static char __cdecl ac_io_icca_device_control_iccard_power_supply_off(int unit_id) {
    return 1;
}

static char __cdecl ac_io_icca_device_control_iccard_power_supply_on(int unit_id) {
    return 1;
}

static bool __cdecl ac_io_icca_device_control_isfinished(int unit_id, DWORD *a2) {
    if (a2 && avs::game::is_model("KFC")) {
        *a2 = 6;
    }

    return true;
}

static bool __cdecl ac_io_icca_get_keep_alive_error(int unit_id, DWORD *a2) {
    *a2 = 0;
    return false;
}

static char __cdecl ac_io_icca_get_status(void *a1, void *a2) {

    // Metal Gear Arcade and Charge Machine had the args swapped so we need to check for valid pointers!
    if (reinterpret_cast<uintptr_t>(a2) > 2) {
        std::swap(a1, a2);

        // honestly this could be used to detect if compat mode should be active
        // but we are too lazy to check if all games still work with this change
        //acio::ICCA_COMPAT_ACTIVE = true;
    }

    // and best just leave this casting mess alone unless something is wrong with it.
    // long long is required because casting to int loses precision on 64-bit
    void *status = a1;
    int unit_id = static_cast<int>(reinterpret_cast<long long>(a2));

    // update state
    unit_id = icca_get_unit_id(unit_id);
    keypad_update(unit_id);
    update_card(unit_id);

    // copy state to output buffer
    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];
    unit->status.status_code = unit->state;
    memcpy(status, &unit->status, sizeof(struct ICCA_STATUS));

    // funny workaround
    if (acio::ICCA_COMPAT_ACTIVE) {
        if (avs::game::is_model("LA9")) {
            auto p = (ICCA_STATUS*) status;

            ICCA_STATUS_LA9 p_la9;
            p_la9.status_code = unit->state;
            p_la9.card_in = unit->card_in;
            memcpy(p_la9.uid, p->uid, sizeof(p_la9.uid));
            p_la9.error = p->error;
            memcpy(p_la9.uid2, p->uid, sizeof(p_la9.uid));

            memcpy(status, &p_la9, sizeof(ICCA_STATUS_LA9));
        } else {
            // the struct is different (28 bytes instead of 24) but nobody ain't got time for that
            auto p = (ICCA_STATUS*) status;
            p->error = p->key_level << 16;
            p->front_sensor = p->uid[0];
            p->rear_sensor = p->uid[1];
            for (size_t i = 2; i < sizeof(p->uid); i++) {
                p->uid[i - 2] = p->uid[i];
            }
            p->uid[sizeof(p->uid) - 2] = 0;
            p->uid[sizeof(p->uid) - 1] = 0;
        }
    }

    return 1;
}

static char __cdecl ac_io_icca_get_uid(int unit_id, char *card) {
    unit_id = icca_get_unit_id(unit_id);
    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];

    // copy card
    memcpy(card, unit->status.uid, 8);

    // set felica flag
    IS_LAST_CARD_FELICA = is_card_uid_felica(unit->status.uid);

    // check for error
    return unit->state != ERR_GETUID;
}

static char __cdecl ac_io_icca_get_uid_felica(int unit_id, char *card) {
    unit_id = icca_get_unit_id(unit_id);
    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];

    // copy card
    memcpy(card, unit->status.uid, 8);

    // set felica flag
    bool felica = is_card_uid_felica(unit->status.uid);
    card[8] = (char) (felica ? 1 : 0);
    IS_LAST_CARD_FELICA = felica;

    if (0 < unit->felica_retries) {
        unit->felica_retries--;
    }

    // check for error
    return unit->state != ERR_GETUID;
}

static bool __cdecl ac_io_icca_is_felica() {
    return IS_LAST_CARD_FELICA;
}

static char __cdecl ac_io_icca_req_uid(int unit_id) {
    unit_id = icca_get_unit_id(unit_id);

    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];
    unit->state = GET_USERID;

    update_card(unit_id);

    return 1;
}

static int __cdecl ac_io_icca_req_uid_isfinished(int unit_id, DWORD *read_state) {
    unit_id = icca_get_unit_id(unit_id);
    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];
    if (unit->card_in) {
        if (fabs(get_performance_seconds() - unit->card_in_time) < CARD_TIMEOUT) {
            unit->state = END;
        } else {
            unit->state = ERR_GETUID;
        }
        unit->card_in = false;
    }
    *read_state = unit->state;
    return 1;
}

static int __cdecl ac_io_icca_send_keep_alive_packet(int a1, int a2, int a3) {
    return 0;
}

static int __cdecl ac_io_icca_workflow(int workflow, int unit_id) {

    unit_id = icca_get_unit_id(unit_id);
    ICCA_UNIT *unit = &ICCA_UNITS[unit_id];
    switch (workflow) {
        case STEP:
            if (avs::game::is_model("JDZ"))
                unit->state = SLEEP;
            else
                unit->state = STEP;
            break;
        case SLEEP:
            unit->state = SLEEP;
            break;
        case INIT:
            unit->state = READY;
            break;
        case START:
            if (unit->card_in)
                unit->state = ACTIVE;
            else
                unit->state = READY;
            break;
        case EJECT:
            unit->card_in = false;
            break;
        case CLOSE_EJECT:
            unit->state = unit->card_in ? EJECT_CHECK : SLEEP;
            break;
        case CLOSE_END:
            unit->state = SLEEP;
            break;
        case GET_USERID:
            unit->state = GET_USERID;
            break;
        default:
            break;
    }

    return unit->state;
}

static char __cdecl ac_io_icca_req_status(int a1, char a2) {
    return 1;
}

static bool __cdecl ac_io_icca_req_status_isfinished(int a1, int *a2) {
    *a2 = 11;
    return true;
}

/*
 * Module stuff
 */

acio::ICCAModule::ICCAModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("ICCA", module, hookMode) {
    this->status_buffer = (uint8_t*) &ICCA_UNITS[0];
    this->status_buffer_size = sizeof(ICCA_UNITS);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::ICCAModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_icca_cardunit_init);
    ACIO_MODULE_HOOK(ac_io_icca_cardunit_init_isfinished);
    ACIO_MODULE_HOOK(ac_io_icca_crypt_init);
    ACIO_MODULE_HOOK(ac_io_icca_device_control_iccard_power_supply_off);
    ACIO_MODULE_HOOK(ac_io_icca_device_control_iccard_power_supply_on);
    ACIO_MODULE_HOOK(ac_io_icca_device_control_isfinished);
    ACIO_MODULE_HOOK(ac_io_icca_get_keep_alive_error);
    ACIO_MODULE_HOOK(ac_io_icca_get_status);
    ACIO_MODULE_HOOK(ac_io_icca_get_uid);
    ACIO_MODULE_HOOK(ac_io_icca_get_uid_felica);
    ACIO_MODULE_HOOK(ac_io_icca_is_felica);
    ACIO_MODULE_HOOK(ac_io_icca_req_uid);
    ACIO_MODULE_HOOK(ac_io_icca_req_uid_isfinished);
    ACIO_MODULE_HOOK(ac_io_icca_send_keep_alive_packet);
    ACIO_MODULE_HOOK(ac_io_icca_workflow);
    ACIO_MODULE_HOOK(ac_io_icca_req_status);
    ACIO_MODULE_HOOK(ac_io_icca_req_status_isfinished);
}
