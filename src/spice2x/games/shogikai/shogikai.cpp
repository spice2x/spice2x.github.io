#include "shogikai.h"

#include "acio/icca/icca.h"
#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#include "io.h"

namespace games::shogikai {

    // touch stuff
    static int TOUCH_MAX_X = 1360;
    static int TOUCH_MAX_Y = 768;
    static bool TOUCH_ATTACHED = false;
    static bool TOUCH_PRESENT = false;
    static bool TOUCH_LAST_PRESENT = false;
    static TouchPoint TOUCH_CURRENT {};
    static TouchPoint TOUCH_LAST {};

    struct joystick_state {
        bool up;
        bool down;
        bool start;
        bool service;
        bool test;
    };

    // general i/o stuff
    static struct joystick_state JOY_STATE {};
    static struct joystick_state JOY_PREVIOUS_STATE {};

    // registry hook
    static decltype(RegOpenKeyExA) *RegOpenKeyExA_orig = nullptr;
    static decltype(RegQueryValueExA) *RegQueryValueExA_orig = nullptr;

    // ini hook
    typedef void (__cdecl *inifile_param_num_t)(const char *, int *);
    static inifile_param_num_t inifile_param_num_orig = nullptr;

    // ic card stuff
    static bool CARD_IN = false;
    static uint8_t CARD_TYPE = 0;
    static uint8_t CARD_UID[8];

    static void __cdecl system_error_set(uint8_t code1, uint16_t code2, uint16_t code3, const char *error_type) {
        if (code1 == 5 && code2 == 1502) {
            log_warning("shogikai", "UNABLE TO SET MONITOR MODE {}", GRAPHICS_WINDOWED ? "" : "(TRY WINDOWED MODE!)");
        }
        log_fatal("shogikai", "{}: {:d}-{:04d}-{:04d}",
                error_type == nullptr ? "UNKNOWN ERROR" : error_type,
                code1, code2, code3);
    }

    static void __cdecl inifile_param_num(const char *param, int *value) {
        if (param && strcmp(param, "WINDOWED") == 0) {
            *value = GRAPHICS_WINDOWED;
        } else if (inifile_param_num_orig) {
            inifile_param_num_orig(param, value);
            if (value) {
                log_info("shogikai", "{}={}", param, *value);
            }
        } else {
            *value = 0;
        }
    }

    static LSTATUS WINAPI RegOpenKeyExA_hook(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions,
            REGSAM samDesired, PHKEY phkResult) {
        if (lpSubKey) {
            if (_stricmp(lpSubKey, "HARDWARE\\DESCRIPTION\\System") == 0) {
                return ERROR_SUCCESS;
            } else if (_stricmp(lpSubKey, "SYSTEM\\CurrentControlSet\\Services\\ialm\\Enum") == 0) {
                return ERROR_SUCCESS;
            }
        }
        return RegOpenKeyExA_orig(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    }

    static LSTATUS WINAPI RegQueryValueExA_hook(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
            LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
        if (lpValueName) {
            if (strcmp(lpValueName, "SystemBiosDate") == 0) {
                static const char DATA[] = "04/20/69";
                memcpy(lpData, DATA, sizeof(DATA));
                return ERROR_SUCCESS;
            } else if (strcmp(lpValueName, "0") == 0) {
                static const char DATA[] = "0";
                memcpy(lpData, DATA, sizeof(DATA));
                return ERROR_SUCCESS;
            }
        }
        return RegQueryValueExA_orig(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    }

    static bool __cdecl nic_dhcp_maybe_exist() {
        return false;
    }

    static void __cdecl mouse_utl_step() {
    }

    static void __cdecl touch_init(int width, int height) {
        log_info("mfc", "call touch_init(width: {}, height: {})", width, height);

        // attach touch module
        if (!TOUCH_ATTACHED) {

            /*
             * Find the game window.
             * We check the foreground window first, then fall back to searching for the window title
             */
            HWND wnd = GetForegroundWindow();
            if (!string_begins_with(GetActiveWindowTitle(), avs::game::MODEL)) {
                wnd = FindWindowBeginsWith(avs::game::MODEL);
            }

            // attach touch hook
            touch_create_wnd(wnd, true);

            // store touch size specification
            TOUCH_MAX_X = width;
            TOUCH_MAX_Y = height;

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
        JOY_STATE.service = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Service));
        JOY_STATE.test = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Test));
        JOY_STATE.start = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Select));
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

    static bool __cdecl mfc5_center_iob_get_coin_state() {
        auto &buttons = get_buttons();

        return GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::CoinMech));
    }

    static int __cdecl mfc5_center_iob_get_coin_cntr(int a1) {
        return eamuse_coin_get_stock();
    }

    static int __cdecl mfc5_center_iob_current_coin_counter() {
        return mfc5_center_iob_get_coin_cntr(0);
    }

    static void __cdecl mfc5_center_iob_consume_coin_counter(int a1) {
        for (int i = 0; i < a1 && a1 >= 0; i++) {
            eamuse_coin_consume_stock();
        }
    }

    static int __cdecl mfc5_current_coin_counter() {
        return eamuse_coin_get_stock();
    }

    static void __cdecl mfc5_consume_coin_counter(int a1) {
        for (int i = 0; i < a1 && a1 >= 0; i++) {
            eamuse_coin_consume_stock();
        }
    }

    static void __cdecl mfc5_iob_lamp(uint8_t lamp) {
        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::Left), (lamp & 0x08) ? 1.f : 0.f);
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::Right), (lamp & 0x04) ? 1.f : 0.f);

        RI_MGR->devices_flush_output();
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

    ShogikaiGame::ShogikaiGame() : Game("Shogikai") {
    }

    void ShogikaiGame::attach() {
        Game::attach();

        // hook error call
        detour::inline_hook((void *) system_error_set, libutils::try_proc(
                libutils::try_module("error.dll"), "?system_error_set@@YAXEGGPAD@Z"));
        detour::iat("?inifile_param_num@@YAHPBDPAH@Z", inifile_param_num);

        // get rid of errors when trying to get BIOS information
        RegOpenKeyExA_orig = detour::iat("RegOpenKeyExA", RegOpenKeyExA_hook);
        RegQueryValueExA_orig = detour::iat("RegQueryValueExA", RegQueryValueExA_hook);

        // network fix
        detour::iat_try("?nic_dhcp_maybe_exist@@YAEXZ", nic_dhcp_maybe_exist);

        // cursor fix
        auto util_module = libutils::try_module("util.dll");
        if (GRAPHICS_SHOW_CURSOR) {
            detour::inline_hook((void *) mouse_utl_step, libutils::try_proc(
                    util_module, "?mouse_utl_step@@YAXXZ"));
        }

        // touch i/o
        auto h8io_module = libutils::try_module("h8io.dll");
        detour::inline_hook((void *) h8io_touch_getpos, libutils::try_proc(
                h8io_module, "?h8io_touch_getpos@@YAHPAH0@Z"));
        detour::inline_hook((void *) h8io_touch_getpos_trig, libutils::try_proc(
                h8io_module, "?h8io_touch_getpos_trig@@YAHPAH0@Z"));
        detour::inline_hook((void *) touch_get_raw_data, libutils::try_proc(
                h8io_module, "?touch_get_raw_data@@YAXPAH0@Z"));
        detour::inline_hook((void *) touch_get_raw_data_trig, libutils::try_proc(
                h8io_module, "?touch_get_raw_data_trig@@YAHPAH0@Z"));
        detour::inline_hook((void *) touch_init, libutils::try_proc(
                h8io_module, "?touch_init@@YAXHH@Z"));
        detour::inline_hook((void *) touch_step, libutils::try_proc(
                h8io_module, "?touch_step@@YAXXZ"));

        // general i/o
        detour::inline_hook((void *) mfc5_begin_io_mng, libutils::try_proc(
                h8io_module, "?mfc5_begin_io_mng@@YAEHH@Z"));
        detour::inline_hook((void *) mfc5_is_io_mng_ready, libutils::try_proc(
                h8io_module, "?mfc5_is_io_mng_ready@@YAHXZ"));
        detour::inline_hook((void *) joystick_step, libutils::try_proc(
                h8io_module, "?joystick_step@@YAXXZ"));
        detour::inline_hook((void *) joy_up, libutils::try_proc(
                h8io_module, "?joy_up@@YAHH@Z"));
        detour::inline_hook((void *) joy_up_on, libutils::try_proc(
                h8io_module, "?joy_up_on@@YAHH@Z"));
        detour::inline_hook((void *) joy_up_rep, libutils::try_proc(
                h8io_module, "?joy_up_rep@@YAHH@Z"));
        detour::inline_hook((void *) joy_down, libutils::try_proc(
                h8io_module, "?joy_down@@YAHH@Z"));
        detour::inline_hook((void *) joy_down_on, libutils::try_proc(
                h8io_module, "?joy_down_on@@YAHH@Z"));
        detour::inline_hook((void *) joy_down_rep, libutils::try_proc(
                h8io_module, "?joy_down_rep@@YAHH@Z"));
        detour::inline_hook((void *) joy_start, libutils::try_proc(
                h8io_module, "?joy_start@@YAHH@Z"));
        detour::inline_hook((void *) joy_start_on, libutils::try_proc(
                h8io_module, "?joy_start_on@@YAHH@Z"));
        detour::inline_hook((void *) joy_start_rep, libutils::try_proc(
                h8io_module, "?joy_start_rep@@YAHH@Z"));
        detour::inline_hook((void *) joy_service, libutils::try_proc(
                h8io_module, "?joy_service@@YAHXZ"));
        detour::inline_hook((void *) joy_service_on, libutils::try_proc(
                h8io_module, "?joy_service_on@@YAHXZ"));
        detour::inline_hook((void *) joy_test, libutils::try_proc(
                h8io_module, "?joy_test@@YAHXZ"));
        detour::inline_hook((void *) joy_test_on, libutils::try_proc(
                h8io_module, "?joy_test_on@@YAHXZ"));
        detour::inline_hook((void *) mfc5_center_iob_get_coin_state, libutils::try_proc(
                h8io_module, "?mfc5_iob_clt_get_coin_state@@YAEXZ"));
        detour::inline_hook((void *) mfc5_center_iob_get_coin_cntr, libutils::try_proc(
                h8io_module, "?mfc5_iob_clt_get_coin_cntr@@YAHW4E_COIN_CH@@@Z"));
        detour::inline_hook((void *) mfc5_center_iob_current_coin_counter, libutils::try_proc(
                h8io_module, "?mfc5_center_iob_current_coin_counter@@YAHXZ"));
        detour::inline_hook((void *) mfc5_center_iob_consume_coin_counter, libutils::try_proc(
                h8io_module, "?mfc5_center_iob_consume_coin_counter@@YAXH@Z"));
        detour::inline_hook((void *) mfc5_current_coin_counter, libutils::try_proc(
                h8io_module, "?mfc5_current_coin_counter@@YAHXZ"));
        detour::inline_hook((void *) mfc5_consume_coin_counter, libutils::try_proc(
                h8io_module, "?mfc5_consume_coin_counter@@YAXH@Z"));
        detour::inline_hook((void *) mfc5_iob_lamp, libutils::try_proc(
                h8io_module, "?mfc5_iob_lamp@@YAXE@Z"));

        // ic card
        detour::inline_hook((void *) mfc5_ic_card_read_init, libutils::try_proc(
                h8io_module, "?mfc5_ic_card_read_init@@YAHXZ"));
        detour::inline_hook((void *) mfc5_ic_card_read_step, libutils::try_proc(
                h8io_module, "?mfc5_ic_card_read_step@@YAHXZ"));
        detour::inline_hook((void *) mfc5_ic_card_status, libutils::try_proc(
                h8io_module, "?mfc5_ic_card_status@@YAHXZ"));
        detour::inline_hook((void *) mfc5_get_ic_card_id_type, libutils::try_proc(
                h8io_module, "?mfc5_get_ic_card_id_type@@YAXPAE@Z"));
        detour::inline_hook((void *) mfc5_get_ic_card_id, libutils::try_proc(
                h8io_module, "?mfc5_get_ic_card_id@@YAXQAE@Z"));
    }

    void ShogikaiGame::detach() {
        Game::detach();
    }
}
