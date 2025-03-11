#include "bi2a.h"

#include "avs/game.h"
#include "games/ddr/io.h"
#include "games/ddr/ddr.h"
#include "games/sdvx/sdvx.h"
#include "games/sdvx/io.h"
#include "games/drs/io.h"
#include "games/drs/drs.h"
#include "misc/eamuse.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/tapeled.h"

using namespace GameAPI;

// state
static uint8_t STATUS_BUFFER[272] {};
static bool STATUS_BUFFER_FREEZE = false;
static unsigned int BI2A_VOLL = 0;
static unsigned int BI2A_VOLR = 0;


static bool __cdecl ac_io_bi2a_init_is_finished() {
    return true;
}

static bool __cdecl ac_io_bi2a_get_control_status_buffer(void *buffer) {

    // copy buffer
    memcpy(buffer, STATUS_BUFFER, std::size(STATUS_BUFFER));
    return true;
}

static bool __cdecl ac_io_bi2a_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // Sound Voltex
    if (avs::game::is_model("KFC")) {

        // clear buffer
        memset(STATUS_BUFFER, 0, std::size(STATUS_BUFFER));
        STATUS_BUFFER[0] = 1;

        /*
         * Unmapped Buttons
         *
         * Control      Bit
         * EX BUTTON 1  93
         * EX BUTTON 2  92
         * EX ANALOG 1  170-183
         * EX ANALOG 2  186-199
         */

        // get buttons
        auto &buttons = games::sdvx::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Test))) {
            ARRAY_SETB(STATUS_BUFFER, 19);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Service))) {
            ARRAY_SETB(STATUS_BUFFER, 18);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::CoinMech))) {
            ARRAY_SETB(STATUS_BUFFER, 17);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Start))) {
            ARRAY_SETB(STATUS_BUFFER, 85);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_A))) {
            ARRAY_SETB(STATUS_BUFFER, 84);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_B))) {
            ARRAY_SETB(STATUS_BUFFER, 83);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_C))) {
            ARRAY_SETB(STATUS_BUFFER, 82);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_D))) {
            ARRAY_SETB(STATUS_BUFFER, 81);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::FX_L))) {
            ARRAY_SETB(STATUS_BUFFER, 80);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::FX_R))) {
            ARRAY_SETB(STATUS_BUFFER, 95);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Headphone))) {
            ARRAY_SETB(STATUS_BUFFER, 87);
        }

        // volume left
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_L_Left))) {
            BI2A_VOLL = (BI2A_VOLL - games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_L_Right))) {
            BI2A_VOLL = (BI2A_VOLL + games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        }

        // volume right
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_R_Left))) {
            BI2A_VOLR = (BI2A_VOLR - games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_R_Right))) {
            BI2A_VOLR = (BI2A_VOLR + games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        }

        // update volumes
        auto &analogs = games::sdvx::get_analogs();
        auto vol_left = BI2A_VOLL;
        auto vol_right = BI2A_VOLR;
        if (analogs.at(0).isSet() || analogs.at(1).isSet()) {
            vol_left += (unsigned int) (Analogs::getState(RI_MGR,
                    analogs.at(games::sdvx::Analogs::VOL_L)) * 1023.99f);
            vol_right += (unsigned int) (Analogs::getState(RI_MGR,
                    analogs.at(games::sdvx::Analogs::VOL_R)) * 1023.99f);
        }

        // proper loops
        vol_left %= 1024;
        vol_right %= 1024;

        // save volumes in buffer
        *((uint16_t*) &STATUS_BUFFER[17]) = (uint16_t) ((vol_left) << 2);
        *((uint16_t*) &STATUS_BUFFER[19]) = (uint16_t) ((vol_right) << 2);
    }

    // DanceDanceRevolution
    if (avs::game::is_model("MDX")) {

        // clear buffer
        memset(STATUS_BUFFER, 0, std::size(STATUS_BUFFER));
        STATUS_BUFFER[0] = 1;

        // get buttons
        auto &buttons = games::ddr::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::COIN_MECH)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[2] |= 1 << 1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::SERVICE)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[2] |= 1 << 2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::TEST)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[2] |= 1 << 3;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P1_START)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[10] |= 1 << 7;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P1_MENU_UP)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[10] |= 1 << 6;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P1_MENU_DOWN)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[10] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P1_MENU_LEFT)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[10] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P1_MENU_RIGHT)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[10] |= 1 << 3;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P2_START)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[11] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P2_MENU_UP)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[11] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P2_MENU_DOWN)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[11] |= 1 << 3;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P2_MENU_LEFT)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[11] |= 1 << 2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ddr::Buttons::P2_MENU_RIGHT)) == Buttons::BUTTON_PRESSED) {
            STATUS_BUFFER[11] |= 1 << 1;
        }
    }

    // DANCERUSH
    if (avs::game::is_model("REC")) {

        // clear buffer
        memset(STATUS_BUFFER, 0, std::size(STATUS_BUFFER));
        STATUS_BUFFER[0] = 1;

        // get buttons
        auto &buttons = games::drs::get_buttons();

        // test
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::Test)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 19);
        }

        // service
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::Service)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 18);
        }

        // coin
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::CoinMech)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 17);
        }

        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P1_Start)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 87);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P1_Up)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 86);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P1_Down)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 85);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P1_Left)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 84);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P1_Right)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 83);
        }

        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P2_Start)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 93);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P2_Up)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 92);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P2_Down)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 91);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P2_Left)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 90);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::drs::Buttons::P2_Right)) == Buttons::State::BUTTON_PRESSED) {
            ARRAY_SETB(STATUS_BUFFER, 89);
        }
    }

    return true;
}

static bool __cdecl ac_io_bi2a_current_coinstock(size_t index, DWORD *coins) {

    // check index
    if (index > 1)
        return false;

    // get coins and return success
    *coins = (DWORD) eamuse_coin_get_stock();
    return true;
}

static bool __cdecl ac_io_bi2a_consume_coinstock(size_t index, int amount) {

    // check index
    if (index > 1)
        return false;

    // calculate new stock
    auto stock = eamuse_coin_get_stock();
    auto stock_new = stock - amount;

    // check new stock
    if (stock_new < 0)
        return false;

    // apply new stock
    eamuse_coin_set_stock(stock_new);
    return true;
}

static bool __cdecl ac_io_bi2a_lock_coincounter(size_t index) {

    // check index
    if (index > 1)
        return false;

    // enable coin blocker
    eamuse_coin_set_block(true);
    return true;
}

static bool __cdecl ac_io_bi2a_unlock_coincounter(size_t index) {

    // check index
    if (index > 1)
        return false;

    // disable coin blocker
    eamuse_coin_set_block(false);
    return true;
}

static void __cdecl ac_io_bi2a_control_coin_blocker_close(size_t index) {

    // check index
    if (index > 1)
        return;

    // enable coin blocker
    eamuse_coin_set_block(true);
}

static void __cdecl ac_io_bi2a_control_coin_blocker_open(size_t index) {

    // check index
    if (index > 1)
        return;

    // disable coin blocker
    eamuse_coin_set_block(false);
}

static long __cdecl ac_io_bi2a_control_led_bright(size_t index, uint8_t brightness) {

    // Sound Voltex
    if (avs::game::is_model("KFC")) {

        /*
         * Control      R   G   B
         * =======================
         * WING UP      28  29  30
         * WING LOW     31  32  33
         * WOOFER       0   1   3
         * CONTROLLER   4   5   6
         *
         * Values go up to 255.
         *
         *
         * Control      Index
         * ==================
         * START BUTTON 8
         * A BUTTON     9
         * B BUTTON     10
         * C BUTTON     11
         * D BUTTON     12
         * FX L BUTTON  13
         * FX R BUTTON  14
         * POP          24
         * TITLE LEFT   25
         * TITLE RIGHT  26
         *
         * Values go up to 127.
         */

        static const struct {
            int light1, light2;
            float max;
        } mapping[] = {
                { games::sdvx::Lights::WOOFER_R, -1, 255 },
                { games::sdvx::Lights::WOOFER_G, -1, 255 },
                { -1, -1, 0 },
                { games::sdvx::Lights::WOOFER_B, -1, 255 },
                { games::sdvx::Lights::CONTROLLER_R, -1, 255 },
                { games::sdvx::Lights::CONTROLLER_G, -1, 255 },
                { games::sdvx::Lights::CONTROLLER_B, -1, 255 },
                { -1, -1, 0 },
                { games::sdvx::Lights::START, -1, 127 },
                { games::sdvx::Lights::BT_A, -1, 127 },
                { games::sdvx::Lights::BT_B, -1, 127 },
                { games::sdvx::Lights::BT_C, -1, 127 },
                { games::sdvx::Lights::BT_D, -1, 127 },
                { games::sdvx::Lights::FX_L, -1, 127 },
                { games::sdvx::Lights::FX_R, -1, 127 },
                { -1, -1, 0 }, { -1, -1, 0 }, { -1, -1, 0 },
                { games::sdvx::Lights::GENERATOR_R, -1, 255 },
                { games::sdvx::Lights::GENERATOR_G, -1, 255 },
                { games::sdvx::Lights::GENERATOR_B, -1, 255 },
                { -1, -1, 0 }, { -1, -1, 0 }, { -1, -1, 0 },
                { games::sdvx::Lights::POP, -1, 127 },
                { games::sdvx::Lights::TITLE_LEFT, -1, 127 },
                { games::sdvx::Lights::TITLE_RIGHT, -1, 127 },
                { -1, -1, 0 },
                { games::sdvx::Lights::WING_RIGHT_UP_R, games::sdvx::Lights::WING_LEFT_UP_R, 255 },
                { games::sdvx::Lights::WING_RIGHT_UP_G, games::sdvx::Lights::WING_LEFT_UP_G, 255 },
                { games::sdvx::Lights::WING_RIGHT_UP_B, games::sdvx::Lights::WING_LEFT_UP_B, 255 },
                { games::sdvx::Lights::WING_RIGHT_LOW_R, games::sdvx::Lights::WING_LEFT_LOW_R, 255 },
                { games::sdvx::Lights::WING_RIGHT_LOW_G, games::sdvx::Lights::WING_LEFT_LOW_G, 255 },
                { games::sdvx::Lights::WING_RIGHT_LOW_B, games::sdvx::Lights::WING_LEFT_LOW_B, 255 },
        };

        // ignore index out of range
        if (index > std::size(mapping)) {
            return true;
        }

        // get lights
        auto &lights = games::sdvx::get_lights();

        // get light from mapping
        auto light = mapping[index];

        // write lights
        if (light.light1 >= 0) {
            Lights::writeLight(RI_MGR, lights[light.light1], brightness / light.max);
        } else {
            log_warning("sdvx", "light unset {} {}", index, (int) brightness);
        }
        if (light.light2 >= 0) {
            Lights::writeLight(RI_MGR, lights[light.light2], brightness / light.max);
        }

    // DANCERUSH
    } else if (avs::game::is_model("REC")) {

        /*
         * Control            R  G  B
         * ==============================
         * CARD UNIT          13 14 15
         * TITLE PANEL        28 29 30
         * MONITOR SIDE LEFT  (tape LED - see ac_io_bi2a_control_tapeled_bright)
         * MONITOR SIDE RIGHT (tape LED - see ac_io_bi2a_control_tapeled_bright)
         * 
         * Values go up to 127.
         *
         * Control      Index
         * ==================
         * 1P LEFT      11
         * 1P RIGHT     12
         * 1P UP        9
         * 1P DOWN      10
         * 1P START     8
         * 2P LEFT      19
         * 2P RIGHT     20
         * 2P UP        17
         * 2P DOWN      18
         * 2P START     16
         * 
         * Values go up to 127.
         */

        static const struct {
            int light;
            float max;
        } mapping[] = {
            { -1, 0 },                                  // 0
            { -1, 0 },                                  // 1
            { -1, 0 },                                  // 2
            { -1, 0 },                                  // 3
            { -1, 0 },                                  // 4
            { -1, 0 },                                  // 5
            { -1, 0 },                                  // 6
            { -1, 0 },                                  // 7
            { games::drs::Lights::P1_START, 127 },      // 8
            { games::drs::Lights::P1_MENU_UP, 127 },    // 9
            { games::drs::Lights::P1_MENU_DOWN, 127 },  // 10
            { games::drs::Lights::P1_MENU_LEFT, 127 },  // 11
            { games::drs::Lights::P1_MENU_RIGHT, 127 }, // 12
            { games::drs::Lights::CARD_READER_R, 127 }, // 13
            { games::drs::Lights::CARD_READER_G, 127 }, // 14
            { games::drs::Lights::CARD_READER_B, 127 }, // 15
            { games::drs::Lights::P2_START, 127 },      // 16
            { games::drs::Lights::P2_MENU_UP, 127 },    // 17
            { games::drs::Lights::P2_MENU_DOWN, 127 },  // 18
            { games::drs::Lights::P2_MENU_LEFT, 127 },  // 19
            { games::drs::Lights::P2_MENU_RIGHT, 127 }, // 20
            { -1, 0 },                                  // 21
            { -1, 0 },                                  // 22
            { -1, 0 },                                  // 23
            { -1, 0 },                                  // 24
            { -1, 0 },                                  // 25
            { -1, 0 },                                  // 26
            { -1, 0 },                                  // 27
            { games::drs::Lights::TITLE_PANEL_R, 127 }, // 28
            { games::drs::Lights::TITLE_PANEL_G, 127 }, // 29
            { games::drs::Lights::TITLE_PANEL_B, 127 }, // 30
        };

        // ignore index out of range
        if (index > std::size(mapping)) {
            return true;
        }

        // get lights
        auto &lights = games::drs::get_lights();

        // get light from mapping
        auto light = mapping[index];

        // write lights
        if (light.light >= 0) {
            Lights::writeLight(RI_MGR, lights[light.light], brightness / light.max);
        } else {
            log_warning("drs", "light unset {} {}", index, (int) brightness);
        }

    // DanceDanceRevolution
    } else if (avs::game::is_model("MDX")) {

        static const struct {
            int light;
            float max;
        } mapping[] = {
            { -1, 0 },                                           // 0
            { -1, 0 },                                           // 1
            { -1, 0 },                                           // 2
            { -1, 0 },                                           // 3
            { -1, 0 },                                           // 4
            { -1, 0 },                                           // 5
            { -1, 0 },                                           // 6
            { -1, 0 },                                           // 7
            { games::ddr::Lights::GOLD_P1_MENU_START, 127 },     // 8
            { games::ddr::Lights::GOLD_P1_MENU_UP, 127 },        // 9
            { games::ddr::Lights::GOLD_P1_MENU_DOWN, 127 },      // 10
            { games::ddr::Lights::GOLD_P1_MENU_LEFT, 127 },      // 11
            { games::ddr::Lights::GOLD_P1_MENU_RIGHT, 127 },     // 12
            { games::ddr::Lights::GOLD_P1_CARD_UNIT_R, 127 },    // 13
            { games::ddr::Lights::GOLD_P1_CARD_UNIT_G, 127 },    // 14
            { games::ddr::Lights::GOLD_P1_CARD_UNIT_B, 127 },    // 15
            { games::ddr::Lights::GOLD_P2_MENU_START, 127 },     // 16
            { games::ddr::Lights::GOLD_P2_MENU_UP, 127 },        // 17
            { games::ddr::Lights::GOLD_P2_MENU_DOWN, 127 },      // 18
            { games::ddr::Lights::GOLD_P2_MENU_LEFT, 127 },      // 19
            { games::ddr::Lights::GOLD_P2_MENU_RIGHT, 127 },     // 20
            { games::ddr::Lights::GOLD_P2_CARD_UNIT_R, 0 },      // 21
            { games::ddr::Lights::GOLD_P2_CARD_UNIT_G, 0 },      // 22
            { games::ddr::Lights::GOLD_P2_CARD_UNIT_B, 0 },      // 23
            { -1, 0 },                                           // 24
            { -1, 0 },                                           // 25
            { -1, 0 },                                           // 26
            { -1, 0 },                                           // 27
            { games::ddr::Lights::GOLD_TITLE_PANEL_LEFT, 0 },    // 28
            { games::ddr::Lights::GOLD_TITLE_PANEL_CENTER, 0 },  // 29
            { games::ddr::Lights::GOLD_TITLE_PANEL_RIGHT, 0 },   // 30
            { games::ddr::Lights::GOLD_P1_WOOFER_CORNER, 0 },    // 31
            { games::ddr::Lights::GOLD_P2_WOOFER_CORNER, 0 }     // 32
        };

        // ignore index out of range
        if (index > std::size(mapping)) {
            return true;
        }

        // get lights
        auto &lights = games::ddr::get_lights();

        // get light from mapping
        auto light = mapping[index];

        // write lights
        if (light.light >= 0) {
            Lights::writeLight(RI_MGR, lights[light.light], brightness / light.max);
        }
    }

    // return success
    return true;
}

static long __cdecl ac_io_bi2a_get_watchdog_time_min() {
    return -1;
}

static long __cdecl ac_io_bi2a_get_watchdog_time_now() {
    return -1;
}

static void __cdecl ac_io_bi2a_watchdog_off() {
}

static bool __cdecl ac_io_bi2a_init(uint8_t param) {
    return true;
}

static bool __cdecl ac_io_bi2a_set_watchdog_time(uint16_t time) {
    return true;
}

static bool __cdecl ac_io_bi2a_get_watchdog_status() {
    return true;
}

static bool __cdecl ac_io_bi2a_set_amp_volume(uint8_t a1, uint8_t a2) {
    return true;
}

static bool __cdecl ac_io_bi2a_tapeled_init(uint8_t a1, uint8_t a2) {
    return true;
}

static bool __cdecl ac_io_bi2a_tapeled_init_is_finished() {
    return true;
}

static bool __cdecl ac_io_bi2a_control_tapeled_rec_set(uint8_t* data, size_t x_sz, size_t y_sz) {

    // check dimensions
    if (x_sz != DRS_TAPELED_COLS || y_sz != DRS_TAPELED_ROWS) {
        log_fatal("drs", "DRS tapeled wrong dimensions");
    }

    // copy data into our buffer - 4 bytes per pixel BGR
    for (size_t i = 0; i < x_sz * y_sz; i++) {
        games::drs::DRS_TAPELED[i][0] = data[i*4+2];
        games::drs::DRS_TAPELED[i][1] = data[i*4+1];
        games::drs::DRS_TAPELED[i][2] = data[i*4];
    }

    // success
    return true;
}

// TODO: DRS tape lights
static bool __cdecl ac_io_bi2a_control_tapeled_bright(size_t off1, size_t off2,
        uint8_t r, uint8_t g, uint8_t b, uint8_t bank) {

    if (!tapeledutils::is_enabled()) {
        return true;
    }

    if (avs::game::is_model("MDX")) {
        
        /*
         * r, g, b values range from [0-255]
         * bank always seems to be [0]
         * 
         * [off1.off2] [LEDs] [tape name]
         *     0.0       25    P1 Foot Up               (0.0 to 0.24, inclusive)
         *     0.25      25    P1 Foot Right
         *     1.0       25    P1 Foot Left
         *     1.25      25    P1 Foot Down
         *
         *     2.0       25    P2 Foot Up
         *     2.25      25    P2 Foot Right
         *     3.0       25    P2 Foot Left
         *     3.25      25    P2 Foot Down
         * 
         *     5.0       50    Top Panel
         *     6.0       50    Monitor side left
         *     7.0       50    Monitor side right
         */

        // In order to set the data that can be output via Spice API, we first
        // need to figure out which device in our buffers this data belongs to
        int device = -1;
        if (off1 == 0) {
            if (off2 < 25) device = 0; // P1 Foot Up
            else device = 1;           // P1 Foot Right
        } else if (off1 == 1) {
            if (off2 < 25) device = 2; // P1 Foot Left
            else device = 3;           // P1 Foot Down
        } else if (off1 == 2) {
            if (off2 < 25) device = 4; // P2 Foot Up
            else device = 5;           // P2 Foot Right
        } else if (off1 == 3) {
            if (off2 < 25) device = 6; // P2 Foot Left
            else device = 7;           // P2 Foot Down
        } else if (off1 >= 5 && off1 <= 7) {
            device = off1 + 3;         // Top Panel / Monitor Side Left / Monitor Side Right
        }

        if (device != -1) {
            // We subtract 25 from off2 to get the device's LED index, if it's for one of the
            // arrow panels that is on the latter half of the logical strip
            size_t subtractor = 0;
            if (off1 <= 3 && off2 >= 25) {
                subtractor = 25;
            }
            size_t led_index = off2 - subtractor;
            
            games::ddr::DDR_TAPELEDS[device][led_index][0] = r;
            games::ddr::DDR_TAPELEDS[device][led_index][1] = g;
            games::ddr::DDR_TAPELEDS[device][led_index][2] = b;
        }

        static struct TapeLedMapping {
            bool split; // true == 50 LEDs for one light, false == 25 for two lights
            uint8_t index_r0, index_g0, index_b0;
            uint8_t index_r1, index_g1, index_b1;
            size_t index_for_avg0 = UINT8_MAX;
            size_t index_for_avg1 = UINT8_MAX;

            TapeLedMapping(
                uint8_t index_r0, uint8_t index_g0, uint8_t index_b0,
                uint8_t index_r1, uint8_t index_g1, uint8_t index_b1)
                    : index_r0(index_r0), index_g0(index_g0), index_b0(index_b0),
                      index_r1(index_r1), index_g1(index_g1), index_b1(index_b1) {

                    split = (index_r1 != UINT8_MAX);
                    if (split) {
                        index_for_avg0 = tapeledutils::get_led_index_using_avg_algo(25);
                        index_for_avg1 = index_for_avg0 + 25;
                    } else {
                        index_for_avg0 = tapeledutils::get_led_index_using_avg_algo(50);
                        index_for_avg1 = -1;
                    }
                }

        } mapping[] = {
            {
                games::ddr::Lights::GOLD_P1_FOOT_UP_AVG_R, games::ddr::Lights::GOLD_P1_FOOT_UP_AVG_G, games::ddr::Lights::GOLD_P1_FOOT_UP_AVG_B,
                games::ddr::Lights::GOLD_P1_FOOT_RIGHT_AVG_R, games::ddr::Lights::GOLD_P1_FOOT_RIGHT_AVG_G, games::ddr::Lights::GOLD_P1_FOOT_RIGHT_AVG_B
            },
            {
                games::ddr::Lights::GOLD_P1_FOOT_LEFT_AVG_R, games::ddr::Lights::GOLD_P1_FOOT_LEFT_AVG_G, games::ddr::Lights::GOLD_P1_FOOT_LEFT_AVG_B,
                games::ddr::Lights::GOLD_P1_FOOT_DOWN_AVG_R, games::ddr::Lights::GOLD_P1_FOOT_DOWN_AVG_G, games::ddr::Lights::GOLD_P1_FOOT_DOWN_AVG_B
            },
            {
                games::ddr::Lights::GOLD_P2_FOOT_UP_AVG_R, games::ddr::Lights::GOLD_P2_FOOT_UP_AVG_G, games::ddr::Lights::GOLD_P2_FOOT_UP_AVG_B,
                games::ddr::Lights::GOLD_P2_FOOT_RIGHT_AVG_R, games::ddr::Lights::GOLD_P2_FOOT_RIGHT_AVG_G, games::ddr::Lights::GOLD_P2_FOOT_RIGHT_AVG_B
            },
            {
                games::ddr::Lights::GOLD_P2_FOOT_LEFT_AVG_R, games::ddr::Lights::GOLD_P2_FOOT_LEFT_AVG_G, games::ddr::Lights::GOLD_P2_FOOT_LEFT_AVG_B,
                games::ddr::Lights::GOLD_P2_FOOT_DOWN_AVG_R, games::ddr::Lights::GOLD_P2_FOOT_DOWN_AVG_G, games::ddr::Lights::GOLD_P2_FOOT_DOWN_AVG_B
            },
            {
                games::ddr::Lights::GOLD_TOP_PANEL_AVG_R, games::ddr::Lights::GOLD_TOP_PANEL_AVG_G, games::ddr::Lights::GOLD_TOP_PANEL_AVG_B,
                UINT8_MAX, UINT8_MAX, UINT8_MAX
            },
            {
                UINT8_MAX, UINT8_MAX, UINT8_MAX,
                UINT8_MAX, UINT8_MAX, UINT8_MAX
            },
            {
                games::ddr::Lights::GOLD_MONITOR_SIDE_LEFT_AVG_R, games::ddr::Lights::GOLD_MONITOR_SIDE_LEFT_AVG_G, games::ddr::Lights::GOLD_MONITOR_SIDE_LEFT_AVG_B,
                UINT8_MAX, UINT8_MAX, UINT8_MAX
            },
            {
                games::ddr::Lights::GOLD_MONITOR_SIDE_RIGHT_AVG_R, games::ddr::Lights::GOLD_MONITOR_SIDE_RIGHT_AVG_G, games::ddr::Lights::GOLD_MONITOR_SIDE_RIGHT_AVG_B,
                UINT8_MAX, UINT8_MAX, UINT8_MAX
            },
        };

        if (off1 < std::size(mapping)) {
            auto &map = mapping[off1];
            
            size_t off2_match = -1;
            if (!map.split || off2 < 25) {
                off2_match = map.index_for_avg0;
            } else {
                off2_match = map.index_for_avg1;
            }
            if (off2_match == off2 && map.index_r0 != UINT8_MAX) {
                auto &lights = games::ddr::get_lights();
                if (!map.split || off2 < 25) {
                    Lights::writeLight(RI_MGR, lights[map.index_r0], r / 255.f);
                    Lights::writeLight(RI_MGR, lights[map.index_g0], g / 255.f);
                    Lights::writeLight(RI_MGR, lights[map.index_b0], b / 255.f);
                } else {
                    Lights::writeLight(RI_MGR, lights[map.index_r1], r / 255.f);
                    Lights::writeLight(RI_MGR, lights[map.index_g1], g / 255.f);
                    Lights::writeLight(RI_MGR, lights[map.index_b1], b / 255.f);
                }
            }
        }
    }

    return true;
}

static bool __cdecl ac_io_bi2a_tapeled_send() {
    return true;
}

static int __cdecl ac_io_bi2a_get_exbio2_status(uint8_t *info) {
    // surely this meme never gets old
    info[5] = 5;
    info[6] = 7;
    info[7] = 3;
    return 0;
}

acio::BI2AModule::BI2AModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("BI2A", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::BI2AModule::attach() {
    ACIOModule::attach();

    ACIO_MODULE_HOOK(ac_io_bi2a_init_is_finished);
    ACIO_MODULE_HOOK(ac_io_bi2a_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_bi2a_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_bi2a_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_bi2a_consume_coinstock);
    ACIO_MODULE_HOOK(ac_io_bi2a_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_bi2a_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_bi2a_control_coin_blocker_close);
    ACIO_MODULE_HOOK(ac_io_bi2a_control_coin_blocker_open);
    ACIO_MODULE_HOOK(ac_io_bi2a_control_led_bright);
    ACIO_MODULE_HOOK(ac_io_bi2a_get_watchdog_time_min);
    ACIO_MODULE_HOOK(ac_io_bi2a_get_watchdog_time_now);
    ACIO_MODULE_HOOK(ac_io_bi2a_watchdog_off);
    ACIO_MODULE_HOOK(ac_io_bi2a_init);
    ACIO_MODULE_HOOK(ac_io_bi2a_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_bi2a_get_watchdog_status);
    ACIO_MODULE_HOOK(ac_io_bi2a_set_amp_volume);
    ACIO_MODULE_HOOK(ac_io_bi2a_tapeled_init);
    ACIO_MODULE_HOOK(ac_io_bi2a_tapeled_init_is_finished);
    ACIO_MODULE_HOOK(ac_io_bi2a_get_exbio2_status);
    ACIO_MODULE_HOOK(ac_io_bi2a_control_tapeled_rec_set);
    ACIO_MODULE_HOOK(ac_io_bi2a_control_tapeled_bright);
    ACIO_MODULE_HOOK(ac_io_bi2a_tapeled_send);
}
