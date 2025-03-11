#include "device.h"

#include <ctime>

#include "avs/game.h"
#include "cfg/api.h"
#include "games/gitadora/io.h"
#include "games/jb/jb.h"
#include "games/jb/io.h"
#include "games/rb/io.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"
#include "rawinput/rawinput.h"

#include "eamuse.h"

using namespace GameAPI;

// settings
const char DEVICE_SYSTEM_VERSION[] = "4.2.0:0";
const char DEVICE_SUBBOARD_VERSION[] = "4.2.0:0";

// state
static HINSTANCE DEVICE_INSTANCE;
static std::string DEVICE_INSTANCE_NAME1 = "device.dll";
static std::string DEVICE_INSTANCE_NAME2 = "libdevice.dll";
static bool DEVICE_INITIALIZED = false;
static int DEVICE_INPUT_STATE;

static int __cdecl device_check_secplug(int a1) {

    // check for invalid index
    if (a1 > 1) {
        return 0;
    }

    // J44/K44 has it flipped
    if (avs::game::is_model({ "J44", "K44" })) {
        return 0x101 - a1;
    } else {
        return 0x100 + a1;
    }
}

static int __cdecl device_force_check_secplug(int a1, int a2) {
    return 0;
}

static short __cdecl device_dispose_coinstock() {
    eamuse_coin_consume_stock();
    return 0;
}

static int __cdecl device_finalize(int a1, int a2) {
    return 0;
}

static void __cdecl device_get_coinstock(unsigned short *coin1, unsigned short *coin2) {
    *coin1 = (unsigned short) eamuse_coin_get_stock();
    *coin2 = 0;

    // without this, jubeat will spawn never ending credit inserts
    eamuse_coin_consume_stock();
}

static void __cdecl device_get_coinstock_all(unsigned short *coin1, unsigned short *coin2) {
    *coin1 = (unsigned short) eamuse_coin_get_stock();
    *coin2 = 0;

    // without this, jubeat will spawn never ending credit inserts
    eamuse_coin_consume_stock();
}

static char __cdecl device_get_dispw() {
    return 0;
}

static long __cdecl device_get_input(int a1) {

    // Gitadora
    if (avs::game::is_model({ "J32", "J33", "K32", "K33", "L32", "L33", "M32" })) {
        long ret = 0;

        // get buttons
        auto &buttons = games::gitadora::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::Service))) {
            ret |= 0x8;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::Test))) {
            ret |= 0x2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::Coin))) {
            ret |= 0x10;
        }

        // gf player 2 controls
        if (a1 == 1) {

            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Start))) {
                ret |= 0x4;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Up))) {
                ret |= 0x20;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Down))) {
                ret |= 0x40;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Left))) {
                ret |= 0x80;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Right))) {
                ret |= 0x100;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Help))) {
                ret |= 0x200;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Effect1))) {
                ret |= 0x400;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Effect2))) {
                ret |= 0x800;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2Effect3))) {
                ret |= 0x1000;
            }
            if (!Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2EffectPedal))) {
                ret |= 0x2000;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2ButtonExtra1))) {
                ret |= 0x4000;
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP2ButtonExtra2))) {
                ret |= 0x8000;
            }

            // return flags
            return ret;
        }

        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Start)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumStart)))
        {
            ret |= 0x4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Up)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumUp)))
        {
            ret |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Down)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumDown)))
        {
            ret |= 0x40;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Left)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumLeft)))
        {
            ret |= 0x80;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Right)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumRight)))
        {
            ret |= 0x100;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Help)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumHelp)))
        {
            ret |= 0x200;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Effect1))) {
            ret |= 0x400;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Effect2))) {
            ret |= 0x800;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1Effect3))) {
            ret |= 0x1000;
        }
        if (!Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1EffectPedal))) {
            ret |= 0x2000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1ButtonExtra1)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumButtonExtra1)))
        {
            ret |= 0x4000;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::GuitarP1ButtonExtra2)) ||
            Buttons::getState(RI_MGR, buttons.at(games::gitadora::Buttons::DrumButtonExtra2)))
        {
            ret |= 0x8000;
        }

        // return flags
        return ret;
    }

    // all other games get updated in device_update()
    if (a1) {
        return 0;
    }

    return DEVICE_INPUT_STATE;
}

static long __cdecl device_get_input_time() {
    time_t t = std::time(nullptr);
    auto now = static_cast<long int> (t);
    return now;
}

static int *__cdecl device_get_jamma() {
    return &DEVICE_INPUT_STATE;
}

static int __cdecl device_get_jamma_history(struct T_JAMMA_HISTORY_INFO *a1, int a2) {
    return 0;
}

static int __cdecl device_get_panel_trg_off(int a1, int a2, int a3) {
    return 0;
}

static int __cdecl device_get_panel_trg_on(int a1, int a2, int a3) {
    return 0;
}

static int __cdecl device_get_panel_trg_short_on(int a1, int a2, int a3) {
    return 0;
}

static int __cdecl device_get_secplug_error(int a1) {
    return 0;
}

static int __cdecl device_get_secplug_hist(int a1, int a2, char *a3) {
    *a3 = 0;
    return 0;
}

static int __cdecl device_get_racecount() {
    return 0;
}

static int __cdecl device_get_secplug(int a1, int a2, int a3) {
    return 1;
}

static int __cdecl device_get_sliptrg(int a1) {
    return 0;
}

static int __cdecl device_get_status() {
    return 0;
}

static int __cdecl device_get_subboard_version(void *data, unsigned int size) {
    if (size < sizeof(DEVICE_SUBBOARD_VERSION)) {
        memset(data, 0, MIN(1, size));
    } else {
        memcpy(data, DEVICE_SUBBOARD_VERSION, sizeof(DEVICE_SUBBOARD_VERSION));
    }
    return 0;
}

static const char *__cdecl device_get_sys_version() {
    return DEVICE_SYSTEM_VERSION;
}

static int __cdecl device_initialize(int a1, int a2) {
    DEVICE_INITIALIZED = true;
    return 0;
}

static bool __cdecl device_is_initialized() {
    return DEVICE_INITIALIZED;
}

static void __cdecl device_poweroff() {
}

static int __cdecl device_read_secplug(int a1, int a2, int a3) {
    return 1;
}

static int __cdecl device_set_coinblocker_open(char number, char open) {
    return 0;
}

static void __cdecl device_set_coincounter_merge() {
}

static int __cdecl device_set_coincounter_work() {
    return 0;
}

static void __cdecl device_set_coincounter_controllable() {
}

static int __cdecl device_set_panel_mode(int mode) {
    return 0;
}

static void __cdecl device_set_jamma_asyncmode() {
}

static void __cdecl device_set_jamma_normalmode() {
}

static void __cdecl device_set_jamma_unti_inputskip(int a1) {
}

static int __cdecl device_set_portout(size_t index, int value) {

    // reflec beat
    if (avs::game::is_model("MBR")) {

        // get lights
        auto &lights = games::rb::get_lights();

        // mapping
        static const size_t light_mapping[] {
            games::rb::Lights::WooferR,
            games::rb::Lights::WooferG,
            games::rb::Lights::WooferB,
            games::rb::Lights::EscutcheonR,
            games::rb::Lights::EscutcheonG,
            games::rb::Lights::EscutcheonB,
            games::rb::Lights::TitleR,
            games::rb::Lights::TitleG,
            games::rb::Lights::TitleB,
            games::rb::Lights::TitleUpR,
            games::rb::Lights::TitleUpG,
            games::rb::Lights::TitleUpB,
        };

        // set light
        if (index < std::size(light_mapping)) {
            Lights::writeLight(RI_MGR, lights.at(light_mapping[index]), value / 127.f);
        }
    }

    return 0;
}

static void __cdecl device_set_portoutbit() {
}

static int __cdecl device_set_watchdog_timer(int a1) {
    return 0;
}

static void __cdecl device_set_watchdog() {
    return;
}

static void __cdecl device_update() {

    // flush device output
    RI_MGR->devices_flush_output();

    // JB knit and copious
    if (avs::game::is_model({ "J44", "K44" })) {

        // update touch
        games::jb::touch_update();

        // get buttons
        auto &buttons = games::jb::get_buttons();

        // reset
        DEVICE_INPUT_STATE = 0;

        if (Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Service))) {
            DEVICE_INPUT_STATE |= 1 << 30;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Test))) {
            DEVICE_INPUT_STATE |= 1 << 28;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::CoinMech))) {
            DEVICE_INPUT_STATE |= 1 << 29;
        }
        if (games::jb::TOUCH_STATE[3] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button1))) {
            DEVICE_INPUT_STATE |= 1 << 13;
        }
        if (games::jb::TOUCH_STATE[7] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button2))) {
            DEVICE_INPUT_STATE |= 1 << 9;
        }
        if (games::jb::TOUCH_STATE[11] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button3))) {
            DEVICE_INPUT_STATE |= 1 << 21;
        }
        if (games::jb::TOUCH_STATE[15] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button4))) {
            DEVICE_INPUT_STATE |= 1 << 17;
        }
        if (games::jb::TOUCH_STATE[2] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button5))) {
            DEVICE_INPUT_STATE |= 1 << 14;
        }
        if (games::jb::TOUCH_STATE[6] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button6))) {
            DEVICE_INPUT_STATE |= 1 << 10;
        }
        if (games::jb::TOUCH_STATE[10] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button7))) {
            DEVICE_INPUT_STATE |= 1 << 22;
        }
        if (games::jb::TOUCH_STATE[14] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button8))) {
            DEVICE_INPUT_STATE |= 1 << 18;
        }
        if (games::jb::TOUCH_STATE[1] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button9))) {
            DEVICE_INPUT_STATE |= 1 << 15;
        }
        if (games::jb::TOUCH_STATE[5] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button10))) {
            DEVICE_INPUT_STATE |= 1 << 11;
        }
        if (games::jb::TOUCH_STATE[9] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button11))) {
            DEVICE_INPUT_STATE |= 1 << 23;
        }
        if (games::jb::TOUCH_STATE[13] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button12))) {
            DEVICE_INPUT_STATE |= 1 << 19;
        }
        if (games::jb::TOUCH_STATE[0] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button13))) {
            DEVICE_INPUT_STATE |= 1 << 24;
        }
        if (games::jb::TOUCH_STATE[4] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button14))) {
            DEVICE_INPUT_STATE |= 1 << 12;
        }
        if (games::jb::TOUCH_STATE[8] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button15))) {
            DEVICE_INPUT_STATE |= 1 << 26;
        }
        if (games::jb::TOUCH_STATE[12] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button16))) {
            DEVICE_INPUT_STATE |= 1 << 20;
        }

        DEVICE_INPUT_STATE = ~DEVICE_INPUT_STATE;
    }

    // JB
    if (avs::game::is_model("L44")) {

        // update touch
        games::jb::touch_update();

        // get buttons
        auto &buttons = games::jb::get_buttons();

        // reset
        DEVICE_INPUT_STATE = 0;

        if (Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Service))) {
            DEVICE_INPUT_STATE |= 1 << 25;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Test))) {
            DEVICE_INPUT_STATE |= 1 << 28;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::CoinMech))) {
            DEVICE_INPUT_STATE |= 1 << 24;
        }
        if (games::jb::TOUCH_STATE[0] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button1))) {
            DEVICE_INPUT_STATE |= 1 << 5;
        }
        if (games::jb::TOUCH_STATE[1] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button2))) {
            DEVICE_INPUT_STATE |= 1 << 1;
        }
        if (games::jb::TOUCH_STATE[2] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button3))) {
            DEVICE_INPUT_STATE |= 1 << 13;
        }
        if (games::jb::TOUCH_STATE[3] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button4))) {
            DEVICE_INPUT_STATE |= 1 << 9;
        }
        if (games::jb::TOUCH_STATE[4] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button5))) {
            DEVICE_INPUT_STATE |= 1 << 6;
        }
        if (games::jb::TOUCH_STATE[5] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button6))) {
            DEVICE_INPUT_STATE |= 1 << 2;
        }
        if (games::jb::TOUCH_STATE[6] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button7))) {
            DEVICE_INPUT_STATE |= 1 << 14;
        }
        if (games::jb::TOUCH_STATE[7] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button8))) {
            DEVICE_INPUT_STATE |= 1 << 10;
        }
        if (games::jb::TOUCH_STATE[8] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button9))) {
            DEVICE_INPUT_STATE |= 1 << 7;
        }
        if (games::jb::TOUCH_STATE[9] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button10))) {
            DEVICE_INPUT_STATE |= 1 << 3;
        }
        if (games::jb::TOUCH_STATE[10] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button11))) {
            DEVICE_INPUT_STATE |= 1 << 15;
        }
        if (games::jb::TOUCH_STATE[11] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button12))) {
            DEVICE_INPUT_STATE |= 1 << 11;
        }
        if (games::jb::TOUCH_STATE[12] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button13))) {
            DEVICE_INPUT_STATE |= 1 << 16;
        }
        if (games::jb::TOUCH_STATE[13] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button14))) {
            DEVICE_INPUT_STATE |= 1 << 4;
        }
        if (games::jb::TOUCH_STATE[14] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button15))) {
            DEVICE_INPUT_STATE |= 1 << 20;
        }
        if (games::jb::TOUCH_STATE[15] || Buttons::getState(RI_MGR, buttons.at(games::jb::Buttons::Button16))) {
            DEVICE_INPUT_STATE |= 1 << 12;
        }
    }

    // RB
    if (avs::game::is_model({ "KBR", "LBR", "MBR" })) {

        // get buttons
        auto &buttons = games::rb::get_buttons();

        // reset
        DEVICE_INPUT_STATE = 0;

        if (Buttons::getState(RI_MGR, buttons.at(games::rb::Buttons::Service))) {
            DEVICE_INPUT_STATE |= 0x08;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rb::Buttons::Test))) {
            DEVICE_INPUT_STATE |= 0x02;
        }
    }
}

static void __cdecl device_update_secplug() {
}

static int __cdecl devsci_break(char a1, char a2) {
    return 0;
}

static int __cdecl devsci_open(int a1, int a2) {
    return 0;
}

static int __cdecl devsci_read(int a1, int a2) {
    return 0;
}

static int __cdecl devsci_write(int a1, int a2, int a3) {
    return a3;
}

static int __cdecl p4io_sci_boot() {
    return 1;
}

static int __cdecl p4io_sci_close(int index) {
    return 0;
}

static int __cdecl p4io_sci_set_linebreak(int index, char c) {
    return 0;
}

static int __cdecl p4io_sci_setparam(int a1, int a2, int a3, uint8_t a4) {
    return 1;
}

static int __cdecl p4io_sci_puts(int index, const char *msg, int size) {
    return 0;
}

static int __cdecl p4io_sci_gets(int index, uint8_t *buffer, int buffer_size) {
    return 0;
}

static int __cdecl p4io_sci_flush() {
    return 0;
}

static int __cdecl p4io_sci_flush_complete() {
    return 1;
}

static int __cdecl p4io_sci_clear_error(int index) {

    // return cleared error
    return 0;
}

static int __cdecl p4io_sci_get_error(int index) {
    return 0;
}

static int __cdecl p4io_sci_print_error(int index, int (__fastcall *logger)(const char *)) {
    return 0;
}

void spicedevice_attach() {

    // get instance
    DEVICE_INSTANCE = libutils::try_module(DEVICE_INSTANCE_NAME1);
    if (!DEVICE_INSTANCE) {
        DEVICE_INSTANCE = libutils::try_module(DEVICE_INSTANCE_NAME2);
    }
    if (!DEVICE_INSTANCE) {
        log_info("device", "skipping device module hooks");
        return;
    }

    log_info("device", "SpiceTools DEVICE");

    /*
     * Patches
     * the trick here is to account for normal names and specific ones
     */
    detour::inline_hook((void *) device_check_secplug, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_check_secplug", "?device_check_secplug@@YAHH@Z"}));
    detour::inline_hook((void *) device_force_check_secplug, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_force_check_secplug", "?device_force_check_secplug@@YAXXZ"}));
    detour::inline_hook((void *) device_dispose_coinstock, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_dispose_coinstock"}));
    detour::inline_hook((void *) device_finalize, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_finalize", "?device_finalize@@YAXXZ"}));
    detour::inline_hook((void *) device_get_coinstock, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_coinstock", "?device_get_coinstock@@YAXPEAG0@Z"}));
    detour::inline_hook((void *) device_get_coinstock_all, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_coinstock_all",
                              "?device_get_coinstock_all@@YAXPEAG0@Z",
                              "?device_get_coinstock_all@@YAXPAG0@Z"}));
    detour::inline_hook((void *) device_get_dispw, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_dispw", "?device_get_dipsw@@YAEH@Z"}));
    detour::inline_hook((void *) device_get_input, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_input", "?device_get_input@@YAIH@Z"}));
    detour::inline_hook((void *) device_get_input_time, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_input_time", "?device_get_input_time@@YA_KXZ"}));
    detour::inline_hook((void *) device_get_jamma, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_jamma"}));
    detour::inline_hook((void *) device_get_jamma_history, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_jamma_history",
                              "?device_get_jamma_history@@YAHPEAUT_JAMMA_HISTORY_INFO@@H@Z",
                              "?device_get_jamma_history@@YAHPAUT_JAMMA_HISTORY_INFO@@H@Z"}));
    detour::inline_hook((void *) device_get_panel_trg_off, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_panel_trg_off"}));
    detour::inline_hook((void *) device_get_panel_trg_on, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_panel_trg_on"}));
    detour::inline_hook((void *) device_get_panel_trg_short_on, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_panel_trg_short_on"}));
    detour::inline_hook((void *) device_get_secplug_error, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_secplug_error"}));
    detour::inline_hook((void *) device_get_secplug_hist, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_secplug_hist"}));
    detour::inline_hook((void *) device_get_racecount, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_racecount", "?device_get_racecount@@YAHXZ"}));
    detour::inline_hook((void *) device_get_secplug, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_secplug", "?device_get_secplug@@YAHHQEAE0@Z"}));
    detour::inline_hook((void *) device_get_sliptrg, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_sliptrg", "?device_get_sliptrg@@YAIH@Z"}));
    detour::inline_hook((void *) device_get_status, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_status", "?device_get_status@@YAHXZ"}));
    detour::inline_hook((void *) device_get_subboard_version, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_subboard_version",
                              "?device_get_subboard_version@@YAHPEADH@Z",
                              "?device_get_subboard_version@@YAHPADH@Z"}));
    detour::inline_hook((void *) device_get_sys_version, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_get_sys_version", "?device_get_sys_version@@YAPEBDXZ"}));
    detour::inline_hook((void *) device_initialize, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_initialize",
                              "?device_initialize@@YAHH@Z",
                              "?device_initialize@@YAHXZ"}));
    detour::inline_hook((void *) device_is_initialized, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_is_initialized"}));
    detour::inline_hook((void *) device_poweroff, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_poweroff", "?device_poweroff@@YAXXZ"}));
    detour::inline_hook((void *) device_read_secplug, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_read_secplug", "?device_read_secplug@@YAHHQEAE0@Z"}));
    detour::inline_hook((void *) device_set_coinblocker_open, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_coinblocker_open", "?device_set_coinblocker_open@@YAXEE@Z"}));
    detour::inline_hook((void *) device_set_coincounter_merge, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_coincounter_merge", "?device_set_coincounter_merge@@YAXE@Z"}));
    detour::inline_hook((void *) device_set_coincounter_work, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_coincounter_work", "?device_set_coincounter_work@@YAXEE@Z"}));
    detour::inline_hook((void *) device_set_coincounter_controllable, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_coincounter_controllable", "?device_set_coincounter_controllable@@YAXEE@Z"}));
    detour::inline_hook((void *) device_set_panel_mode, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_panel_mode"}));
    detour::inline_hook((void *) device_set_jamma_asyncmode, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_jamma_asyncmode", "?device_set_jamma_asyncmode@@YAXXZ"}));
    detour::inline_hook((void *) device_set_jamma_normalmode, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_jamma_normalmode", "?device_set_jamma_normalmode@@YAXXZ"}));
    detour::inline_hook((void *) device_set_jamma_unti_inputskip, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_jamma_unti_inputskip", "?device_set_jamma_unti_inputskip@@YAXH@Z"}));
    detour::inline_hook((void *) device_set_portout, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_portout", "?device_set_portout@@YAXHH@Z"}));
    detour::inline_hook((void *) device_set_portoutbit, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_portoutbit", "?device_set_portoutbit@@YAXHH@Z"}));
    detour::inline_hook((void *) device_set_watchdog_timer, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_set_watchdog_timer"}));
    detour::inline_hook((void *) device_set_watchdog, libutils::try_proc_list(
            DEVICE_INSTANCE, {"?device_set_watchdog@@YAXH@Z"}));
    detour::inline_hook((void *) device_update, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_update", "?device_update@@YAXXZ"}));
    detour::inline_hook((void *) device_update_secplug, libutils::try_proc_list(
            DEVICE_INSTANCE, {"device_update_secplug", "?device_update_secplug@@YAXXZ"}));
    detour::inline_hook((void *) devsci_break, libutils::try_proc_list(
            DEVICE_INSTANCE, {"devsci_break", "?devsci_break@@YAXHH@Z"}));
    detour::inline_hook((void *) devsci_open, libutils::try_proc_list(
            DEVICE_INSTANCE, {"devsci_open", "?devsci_open@@YAXHHH@Z"}));
    detour::inline_hook((void *) devsci_read, libutils::try_proc_list(
            DEVICE_INSTANCE, {"devsci_read", "?devsci_read@@YAHHPEAEH@Z"}));
    detour::inline_hook((void *) devsci_write, libutils::try_proc_list(
            DEVICE_INSTANCE, {"devsci_write", "?devsci_write@@YAHHPEBEH@Z"}));

    detour::inline_hook((void *) p4io_sci_boot, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_boot"}));
    detour::inline_hook((void *) p4io_sci_close, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_close"}));
    detour::inline_hook((void *) p4io_sci_set_linebreak, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_set_linebreak"}));
    detour::inline_hook((void *) p4io_sci_setparam, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_setparam"}));
    detour::inline_hook((void *) p4io_sci_puts, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_puts"}));
    detour::inline_hook((void *) p4io_sci_gets, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_gets"}));
    detour::inline_hook((void *) p4io_sci_flush, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_flush"}));
    detour::inline_hook((void *) p4io_sci_flush_complete, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_flush_complete"}));
    detour::inline_hook((void *) p4io_sci_clear_error, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_clear_error"}));
    detour::inline_hook((void *) p4io_sci_get_error, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_get_error"}));
    detour::inline_hook((void *) p4io_sci_print_error, libutils::try_proc_list(
            DEVICE_INSTANCE, {"p4io_sci_print_error"}));

    // TODO: missing functions
    // device_hwinfo_etc_update
    // device_hwinfo_get
    // device_hwinfo_update
    // device_set_jamma_interrupt_status
    // sci_mng_get_debug_info
    // test_device
}

void spicedevice_detach() {
    // TODO
}
