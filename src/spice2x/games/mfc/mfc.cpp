#include "mfc.h"

#include "acio/icca/icca.h"
#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "misc/eamuse.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#include "io.h"

namespace games::mfc {

    // touch stuff
    static int TOUCH_MAX_X = 1360;
    static int TOUCH_MAX_Y = 768;
    static bool TOUCH_ATTACHED = false;
    static bool TOUCH_PRESENT = false;
    static bool TOUCH_LAST_PRESENT = false;
    static TouchPoint TOUCH_CURRENT {};
    static TouchPoint TOUCH_LAST {};

    // general i/o stuff
    static struct joystick_state JOY_STATE {};
    static struct joystick_state JOY_PREVIOUS_STATE {};

    // ic card stuff
    static bool CARD_IN = false;
    static uint8_t CARD_TYPE = 0;
    static uint8_t CARD_UID[8];

    typedef int (__cdecl *inifile_param_num_t)(const char *, int *);
    static inifile_param_num_t inifile_param_num_real;

    static int __cdecl inifile_param_num_hook(const char *a1, int *a2) {
        if (strcmp(a1, "WINDOWED") == 0) {
            *a2 = GRAPHICS_WINDOWED ? 1 : 0;
            return 1;
        }
        return inifile_param_num_real(a1, a2);
    }

    static bool __cdecl nic_dhcp_maybe_exist() {
        return true;
    }

    static void __cdecl touch_init(int width, int height) {
        log_info("mfc", "call touch_init(width: {}, height: {})", width, height);

        // attach touch module
        if (!TOUCH_ATTACHED) {

            // store touch size specification
            TOUCH_MAX_X = width;
            TOUCH_MAX_Y = height;

            // attach touch hook
            touch_attach_dx_hook();

            // show cursor
            if (GRAPHICS_SHOW_CURSOR) {
                ShowCursor(TRUE);
            }

            // set attached
            TOUCH_ATTACHED = true;
        }
    }

    static void __cdecl touch_step() {
        if (TOUCH_ATTACHED) {

            // get touch points
            std::vector<TouchPoint> touch_points;
            touch_get_points(touch_points);

            /*
            log_info("MFC", "touch points: " + to_string(touch_points.size()));

            for (TouchPoint tp : touch_points) {
                log_info("MFC", "touch point (id: {}, x: {}, y: {})", tp.id, tp.x, tp.y);
            }
            */

            TOUCH_LAST = TOUCH_CURRENT;
            TOUCH_LAST_PRESENT = TOUCH_PRESENT;

            if (!touch_points.empty()) {
                auto &tp = touch_points[0];

                TOUCH_CURRENT = tp;
                TOUCH_PRESENT = true;
            } else {
                TOUCH_PRESENT = false;
            }
        }
    }

    static int __cdecl h8io_touch_getpos(int *pX, int *pY) {
        if (!TOUCH_PRESENT) {

            // false value means no touch present
            return 0;
        }

        *pX = TOUCH_CURRENT.x > TOUCH_MAX_X ? TOUCH_MAX_X : TOUCH_CURRENT.x;
        *pY = TOUCH_CURRENT.y > TOUCH_MAX_Y ? TOUCH_MAX_Y : TOUCH_CURRENT.y;

        // true value means touch present
        return 1;
    }

    static int __cdecl h8io_touch_getpos_trig(int *pX, int *pY) {

        // following the game logic, a trigger check bails early if there was a touch detected
        // during the last frame
        if (TOUCH_LAST_PRESENT) {
            return 0;
        }

        return h8io_touch_getpos(pX, pY);
    }

    static void __cdecl touch_get_raw_data(int *pX, int *pY) {
        h8io_touch_getpos(pX, pY);
    }

    static int __cdecl touch_get_raw_data_trig(int *pX, int *pY) {
        return h8io_touch_getpos_trig(pX, pY);
    }

    static char __cdecl mfc5_begin_io_mng(int a1, int a2) {
        log_info("mfc", "call mfc5_begin_io_mng");
        return 1;
    }

    static int __cdecl mfc5_is_io_mng_ready() {
        log_info("mfc", "call mfc5_is_io_mng_ready");
        return 1;
    }

    // the joystick state functions return the current state that is updated
    // in `joystick_step` while ensuring to track the previous state to detect
    // held buttons and newly pressed buttons

    static void __cdecl joystick_step() {

        // set last state
        JOY_PREVIOUS_STATE = JOY_STATE;

        // get buttons
        auto &buttons = get_buttons();

        // set new state
        JOY_STATE.up = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::JoystickUp));
        JOY_STATE.down = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::JoystickDown));
        JOY_STATE.start = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::JoystickStart));
        JOY_STATE.service = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Service));
        JOY_STATE.test = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Test));
    }

    static bool __cdecl joy_up(int a1) {
        return JOY_STATE.up;
    }

    static bool __cdecl joy_up_on(int a1) {
        return JOY_STATE.up && !JOY_PREVIOUS_STATE.up;
    }

    static bool __cdecl joy_up_rep(int a1) {
        return JOY_STATE.up && JOY_PREVIOUS_STATE.up;
    }

    static bool __cdecl joy_down(int a1) {
        return JOY_STATE.down;
    }

    static bool __cdecl joy_down_on(int a1) {
        return JOY_STATE.down && !JOY_PREVIOUS_STATE.down;
    }

    static bool __cdecl joy_down_rep(int a1) {
        return JOY_STATE.down && JOY_PREVIOUS_STATE.down;
    }

    static bool __cdecl joy_start(int a1) {
        static bool FIRST_CHECK = true;

        // following the game logic, the first poll of the start button must
        // be true for center mode
        if (FIRST_CHECK) {
            FIRST_CHECK = false;
            return true;
        }

        return JOY_STATE.start;

    }

    static bool __cdecl joy_start_on(int a1) {
        return JOY_STATE.start && !JOY_PREVIOUS_STATE.start;
    }

    static bool __cdecl joy_start_rep(int a1) {
        return JOY_STATE.start && JOY_PREVIOUS_STATE.start;
    }

    static bool __cdecl joy_service() {
        return JOY_STATE.service;
    }

    static bool __cdecl joy_service_on() {
        return JOY_STATE.service && !JOY_PREVIOUS_STATE.service;
    }

    static bool __cdecl joy_test() {
        return JOY_STATE.test;
    }

    static bool __cdecl joy_test_on() {
        return JOY_STATE.test && !JOY_PREVIOUS_STATE.test;
    }

    static void update_card() {
        if (eamuse_card_insert_consume(1, 0)) {

            if (!CARD_IN) {
                CARD_IN = true;

                eamuse_get_card(1, 0, CARD_UID);

                CARD_TYPE = is_card_uid_felica(CARD_UID) ? 1 : 0;
            }
        } else {
            CARD_IN = false;
        }
    }

    static int __cdecl mfc5_ic_card_read_init() {
        //log_misc("mfc", "mfc5_ic_card_read_init");

        CARD_IN = false;
        CARD_TYPE = 0;
        memset(CARD_UID, 0, sizeof(CARD_UID));

        return 0;
    }

    static int __cdecl mfc5_ic_card_read_step() {
        //log_misc("mfc", "mfc5_ic_card_read_step");

        update_card();

        return CARD_IN ? 0 : 1;
    }

    static int __cdecl mfc5_ic_card_status() {
        //log_misc("mfc", "mfc5_ic_card_status");

        return CARD_IN ? 2 : 0;
    }

    static void __cdecl mfc5_get_ic_card_id_type(uint8_t *a1) {
        //log_misc("mfc", "mfc5_get_ic_card_id_type(a1: {})", fmt::ptr(a1));

        *a1 = CARD_TYPE;
    }

    static void __cdecl mfc5_get_ic_card_id(uint8_t *a1) {
        //log_misc("mfc", "mfc5_get_ic_card_id(a1: {})", fmt::ptr(a1));

        memcpy(a1, CARD_UID, 8);
    }

    static void __cdecl mouse_utl_step() {
        return;
    }

    static HCURSOR WINAPI SetCursor_hook(HCURSOR hCursor) {
        log_misc("mfc", "SetCursor hook hit");

        return nullptr;
    }

    MFCGame::MFCGame() : Game("Mahjong Fight Club") {
    }

    void MFCGame::attach() {
        Game::attach();

        // NOTE:
        //
        // develop key binds
        //   F2 SERVICE
        //   S SERVICE
        //   ENTER SELECT
        //   ENTER ENTER (center)
        //   ESC TEST
        //   UP SELECT1 (center)
        //   DOWN SELECT2 (center)

        inifile_param_num_real = (inifile_param_num_t) detour::iat_try(
                "?inifile_param_num@@YAHPBDPAH@Z", (void *) &inifile_param_num_hook, avs::game::DLL_INSTANCE);

        auto allinone_module = libutils::try_module("allinone.dll");
        auto system_module = libutils::try_module("system.dll");

        // network fix
        detour::iat_try("?nic_dhcp_maybe_exist@@YAEXZ", nic_dhcp_maybe_exist, system_module);

        // touch i/o
        detour::inline_hook((void *) h8io_touch_getpos, libutils::try_proc(
                allinone_module, "?h8io_touch_getpos@@YAHPAH0@Z"));
        detour::inline_hook((void *) h8io_touch_getpos_trig, libutils::try_proc(
                allinone_module, "?h8io_touch_getpos_trig@@YAHPAH0@Z"));
        detour::inline_hook((void *) touch_get_raw_data, libutils::try_proc(
                allinone_module, "?touch_get_raw_data@@YAXPAH0@Z"));
        detour::inline_hook((void *) touch_get_raw_data_trig, libutils::try_proc(
                allinone_module, "?touch_get_raw_data_trig@@YAHPAH0@Z"));
        detour::inline_hook((void *) touch_init, libutils::try_proc(
                allinone_module, "?touch_init@@YAXHH@Z"));
        detour::inline_hook((void *) touch_step, libutils::try_proc(
                allinone_module, "?touch_step@@YAXXZ"));

        // general i/o
        detour::inline_hook((void *) mfc5_begin_io_mng, libutils::try_proc(
                allinone_module, "?mfc5_begin_io_mng@@YAEHH@Z"));
        detour::inline_hook((void *) mfc5_is_io_mng_ready, libutils::try_proc(
                allinone_module, "?mfc5_is_io_mng_ready@@YAHXZ"));
        detour::inline_hook((void *) joystick_step, libutils::try_proc(
                allinone_module, "?joystick_step@@YAXXZ"));
        detour::inline_hook((void *) joy_up, libutils::try_proc(
                allinone_module, "?joy_up@@YAHH@Z"));
        detour::inline_hook((void *) joy_up_on, libutils::try_proc(
                allinone_module, "?joy_up_on@@YAHH@Z"));
        detour::inline_hook((void *) joy_up_rep, libutils::try_proc(
                allinone_module, "?joy_up_rep@@YAHH@Z"));
        detour::inline_hook((void *) joy_down, libutils::try_proc(
                allinone_module, "?joy_down@@YAHH@Z"));
        detour::inline_hook((void *) joy_down_on, libutils::try_proc(
                allinone_module, "?joy_down_on@@YAHH@Z"));
        detour::inline_hook((void *) joy_down_rep, libutils::try_proc(
                allinone_module, "?joy_down_rep@@YAHH@Z"));
        detour::inline_hook((void *) joy_start, libutils::try_proc(
                allinone_module, "?joy_start@@YAHH@Z"));
        detour::inline_hook((void *) joy_start_on, libutils::try_proc(
                allinone_module, "?joy_start_on@@YAHH@Z"));
        detour::inline_hook((void *) joy_start_rep, libutils::try_proc(
                allinone_module, "?joy_start_rep@@YAHH@Z"));
        detour::inline_hook((void *) joy_service, libutils::try_proc(
                allinone_module, "?joy_service@@YAHXZ"));
        detour::inline_hook((void *) joy_service_on, libutils::try_proc(
                allinone_module, "?joy_service_on@@YAHXZ"));
        detour::inline_hook((void *) joy_test, libutils::try_proc(
                allinone_module, "?joy_test@@YAHXZ"));
        detour::inline_hook((void *) joy_test_on, libutils::try_proc(
                allinone_module, "?joy_test_on@@YAHXZ"));

        // ic card
        detour::inline_hook((void *) mfc5_ic_card_read_init, libutils::try_proc(
                allinone_module, "?mfc5_ic_card_read_init@@YAHXZ"));
        detour::inline_hook((void *) mfc5_ic_card_read_step, libutils::try_proc(
                allinone_module, "?mfc5_ic_card_read_step@@YAHXZ"));
        detour::inline_hook((void *) mfc5_ic_card_status, libutils::try_proc(
                allinone_module, "?mfc5_ic_card_status@@YAHXZ"));
        detour::inline_hook((void *) mfc5_get_ic_card_id_type, libutils::try_proc(
                allinone_module, "?mfc5_get_ic_card_id_type@@YAXPAE@Z"));
        detour::inline_hook((void *) mfc5_get_ic_card_id, libutils::try_proc(
                allinone_module, "?mfc5_get_ic_card_id@@YAXQAE@Z"));

        if (GRAPHICS_SHOW_CURSOR) {
            detour::iat_try("SetCursor", (void *) SetCursor_hook, avs::game::DLL_INSTANCE);
            detour::iat_try("SetCursor", (void *) SetCursor_hook, allinone_module);

            detour::inline_hook((void *) mouse_utl_step, libutils::try_proc(
                    allinone_module, "?mouse_utl_step@@YAXXZ"));
        }
    }
}
