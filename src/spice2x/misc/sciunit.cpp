#include "sciunit.h"

#include "acio/icca/icca.h"
#include "avs/game.h"
#include "eamuse.h"
#include "games/jb/jb.h"
#include "games/jb/io.h"
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

// function definitions
typedef int (__cdecl *cardunit_card_cardnumber_t)(const uint8_t *const, int, char *, int);

static cardunit_card_cardnumber_t cardunit_card_cardnumber = nullptr;

// state
static HINSTANCE SCIUNIT_INSTANCE;
static std::string SCIUNIT_INSTANCE_NAME = "sciunit.dll";
static bool SCIUNIT_INITIALIZED = false;
static bool CARD_IN = false;
static bool CARD_PRESSED = false;
static uint8_t CARD_UID[8];
static int CARD_TYPE = 1;

static char CARD_DISPLAY_ID[17];
static bool CARD_WAS_CONVERTED = false;

static inline void update_card() {
    if (eamuse_card_insert_consume(1, 0)) {
        if (!CARD_PRESSED) {
            CARD_PRESSED = true;

            // get and check if valid card
            if (!eamuse_get_card(1, 0, CARD_UID)) {
                return;
            }

            CARD_IN = true;
            CARD_WAS_CONVERTED = false;

            CARD_TYPE = is_card_uid_felica(CARD_UID) ? 2 : 1;

            // convert the card UID into the Konami ID if the conversion method was found
            if (cardunit_card_cardnumber != nullptr) {
                auto ret = cardunit_card_cardnumber(CARD_UID, CARD_TYPE, CARD_DISPLAY_ID, sizeof(CARD_DISPLAY_ID));

                CARD_DISPLAY_ID[16] = '\0';
                CARD_WAS_CONVERTED = ret == 0;
            }
        }
    } else {
        CARD_PRESSED = false;
    }
}

static int __cdecl mng_card_check_removed(int /* a1 */) {
    return 0;
}

static int __cdecl mng_card_eject_init(int /* a1 */) {
    CARD_IN = false;
    return 0;
}

static int __cdecl mng_card_get_cardnumber(int a1, uint8_t *out) {
    if (CARD_WAS_CONVERTED) {

        // copy the display id to the output buffer
        memcpy(out, CARD_DISPLAY_ID, 16);
    } else {

        // fallback to displaying the card UID if the conversion method was not
        // found or the conversion to the Konami ID failed
        std::string konami_id = bin2hex(CARD_UID, sizeof(CARD_UID));
        memcpy(out, konami_id.c_str(), 16);
    }

    // ensure the output is null-terminated
    out[16] = '\0';

    return 0;
}

static int __cdecl mng_card_get_cardtype(int /* a1 */) {
    return CARD_TYPE;
}

static int __cdecl mng_card_get_stat(int /* a1 */) {
    return CARD_IN ? 2 : 1;
}

static int __cdecl mng_card_get_uid(int /* a1 */, uint8_t *uid) {
    memcpy(uid, CARD_UID, 8);

    return CARD_IN ? 0 : 1;
}

static int __cdecl mng_card_is_detected(int /* a1 */) {
    return static_cast<int>(CARD_IN);
}

static int __cdecl mng_card_is_ejected(int /* a1 */) {
    return 1;
}

static int __cdecl mng_card_is_inserted(int /* a1 */) {
    update_card();

    return static_cast<int>(CARD_IN);
}

static bool __cdecl mng_card_is_invalid_unit(int /* a1 */) {
    return false;
}

static int __cdecl mng_card_is_read_done(int /* a1 */) {
    update_card();
    return static_cast<int>(CARD_IN);
}

static int __cdecl mng_card_is_ready(int /* a1 */) {
    return 1;
}

static int __cdecl mng_card_is_removed(int /* a1 */) {
    return 1;
}

static int __cdecl mng_card_is_valid_card(int /* a1 */) {
    return static_cast<int>(CARD_IN);
}

static int __cdecl mng_card_read_init(int /* a1 */) {
    return 0;
}

static int __cdecl mng_card_sensor_raw(int /* a1 */) {
    update_card();
    return CARD_IN ? 48 : 0;
}

static void __cdecl mng_card_sleep(int a1) {
}

static int __cdecl sciunit_finalize() {
    return 0;
}

static int __cdecl sciunit_get_errorunit() {
    return 0;
}

static int __cdecl sciunit_get_stat() {
    return 0;
}

static int __cdecl sciunit_get_version(int a1, int a2) {
    return 0;
}

static int __cdecl sciunit_initialize() {
    SCIUNIT_INITIALIZED = true;
    return 0;
}

static int __cdecl sciunit_is_initialized() {
    return SCIUNIT_INITIALIZED ? 1 : 0;
}

static int __cdecl sciunit_is_usable() {
    return 1;
}

static int __cdecl sciunit_led_set_color(uint8_t *led_field) {

    // Jubeat
    if (avs::game::is_model({ "J44", "K44", "L44" })) {

        // get lights
        auto &lights = games::jb::get_lights();

        // control mapping
        //
        // The list in `Lamp Check` is not in the same order as the LED array passed to this
        // function.
        static size_t mapping[] = {
                games::jb::Lights::PANEL_FRONT_R,
                games::jb::Lights::PANEL_FRONT_G,
                games::jb::Lights::PANEL_FRONT_B,
                games::jb::Lights::PANEL_TOP_R,
                games::jb::Lights::PANEL_TOP_G,
                games::jb::Lights::PANEL_TOP_B,
                games::jb::Lights::PANEL_LEFT_R,
                games::jb::Lights::PANEL_LEFT_G,
                games::jb::Lights::PANEL_LEFT_B,
                games::jb::Lights::PANEL_RIGHT_R,
                games::jb::Lights::PANEL_RIGHT_G,
                games::jb::Lights::PANEL_RIGHT_B,
                games::jb::Lights::PANEL_TITLE_R,
                games::jb::Lights::PANEL_TITLE_G,
                games::jb::Lights::PANEL_TITLE_B,
                games::jb::Lights::PANEL_WOOFER_R,
                games::jb::Lights::PANEL_WOOFER_G,
                games::jb::Lights::PANEL_WOOFER_B,
        };

        // write light
        for (size_t i = 0; i < std::size(mapping); i++) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), led_field[i] / 255.f);
        }

        RI_MGR->devices_flush_output();
    }

    return 0;
}

static int __cdecl sciunit_reset() {
    return 0;
}

static int __cdecl sciunit_update() {
    if (SCIUNIT_INITIALIZED)
        update_card();
    return 0;
}

void sciunit_attach() {

    // get instance
    SCIUNIT_INSTANCE = libutils::try_module(SCIUNIT_INSTANCE_NAME);
    if (!SCIUNIT_INSTANCE) {
        log_info("sciunit", "skipping sciunit hooks");
        return;
    }

    log_info("sciunit", "SpiceTools SCIUNIT");

    // get functions
    if (cardunit_card_cardnumber == nullptr) {
        cardunit_card_cardnumber = libutils::try_proc_list<cardunit_card_cardnumber_t>(
                SCIUNIT_INSTANCE, {"cardunit_card_cardnumber", "?cardunit_card_cardnumber@@YAHQBEHPADH@Z"});
    }

    // patch
    detour::inline_hook((void *) mng_card_check_removed, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_check_removed"));
    detour::inline_hook((void *) mng_card_eject_init, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_eject_init"));
    detour::inline_hook((void *) mng_card_get_cardnumber, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_get_cardnumber"));
    detour::inline_hook((void *) mng_card_get_cardtype, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_get_cardtype"));
    detour::inline_hook((void *) mng_card_get_stat, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_get_stat"));
    detour::inline_hook((void *) mng_card_get_uid, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_get_uid"));
    detour::inline_hook((void *) mng_card_is_detected, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_detected"));
    detour::inline_hook((void *) mng_card_is_ejected, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_ejected"));
    detour::inline_hook((void *) mng_card_is_inserted, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_inserted"));
    detour::inline_hook((void *) mng_card_is_invalid_unit, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_invalid_unit"));
    detour::inline_hook((void *) mng_card_is_read_done, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_read_done"));
    detour::inline_hook((void *) mng_card_is_ready, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_ready"));
    detour::inline_hook((void *) mng_card_is_removed, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_removed"));
    detour::inline_hook((void *) mng_card_is_valid_card, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_is_valid_card"));
    detour::inline_hook((void *) mng_card_read_init, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_read_init"));
    detour::inline_hook((void *) mng_card_sensor_raw, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_sensor_raw"));
    detour::inline_hook((void *) mng_card_sleep, libutils::try_proc(
            SCIUNIT_INSTANCE, "mng_card_sleep"));
    detour::inline_hook((void *) sciunit_finalize, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_finalize"));
    detour::inline_hook((void *) sciunit_get_errorunit, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_get_errorunit"));
    detour::inline_hook((void *) sciunit_get_stat, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_get_stat"));
    detour::inline_hook((void *) sciunit_get_version, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_get_version"));
    detour::inline_hook((void *) sciunit_initialize, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_initialize"));
    detour::inline_hook((void *) sciunit_is_initialized, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_is_initialized"));
    detour::inline_hook((void *) sciunit_is_usable, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_is_usable"));
    detour::inline_hook((void *) sciunit_led_set_color, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_led_set_color"));
    detour::inline_hook((void *) sciunit_reset, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_reset"));
    detour::inline_hook((void *) sciunit_update, libutils::try_proc(
            SCIUNIT_INSTANCE, "sciunit_update"));
}

void sciunit_detach() {
    // TODO
}
