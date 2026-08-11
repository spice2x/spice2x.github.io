#include "eamuse.h"

#include <atomic>
#include <chrono>
#include <fstream>

#include "avs/game.h"
#include "cfg/config.h"
#include "games/io.h"
#include "rawinput/rawinput.h"
#include "games/sdvx/sdvx.h"
#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"
#include "overlay/overlay.h"
#include "overlay/notifications.h"

#include "bt5api.h"
#include "misc/hotkeys.h"

// state
static constexpr double NOTIFICATION_THROTTLE_SECONDS = 3.0;
static bool CARD_INSERT[2] = {false, false};
static double CARD_INSERT_TIME[2] = {0, 0};
static double CARD_INSERT_TIMEOUT = 2.0;
static char CARD_INSERT_UID[2][8] = {{0}, {0}};
static char CARD_INSERT_UID_ENABLE[2] = {false, false};
static int COIN_STOCK = 0;
static bool COIN_BLOCK = false;
static uint16_t KEYPAD_STATE[] = {0, 0};
static uint16_t KEYPAD_STATE_OVERRIDES[] = {0, 0};
static uint16_t KEYPAD_STATE_OVERRIDES_BT5[] = {0, 0};
static uint16_t KEYPAD_STATE_OVERRIDES_READER[] = {0, 0};
static uint16_t KEYPAD_STATE_OVERRIDES_OVERLAY[] = {0, 0};
static std::string EAMUSE_GAME_NAME;
static ConfigKeypadBindings KEYPAD_BINDINGS {};

// auto card
bool AUTO_INSERT_CARD[2] = {false, false};
float AUTO_INSERT_CARD_COOLDOWN = 8.f; // seconds
static std::optional<double> AUTO_INSERT_CARD_FIRST_CONSUME_TIME;
static bool AUTO_INSERT_CARD_CACHED[2];
static uint8_t AUTO_INSERT_CARD_CACHED_DATA[2][8];

// pin macro
bool PIN_MACRO_ENABLED = false;
std::string PIN_MACRO_VALUES[2] = {"", ""};
std::string AUTO_PIN_MACRO_TRIGGER[2];
static std::atomic_bool AUTO_PIN_MACRO_REQUEST[2] {false, false};
static bool AUTO_PIN_MACRO_PLAYER_ACTIVE[2] = {false, false};
static std::mutex PIN_MACRO_MUTEX;
static std::atomic_bool PIN_MACRO_ACTIVE {false};

enum class PinMacroPhase {
    Idle,
    KeyDown,
    InterDigit,
    Cooldown,
};

static PinMacroPhase PIN_MACRO_PHASE = PinMacroPhase::Idle;
static std::optional<size_t> PIN_MACRO_ACTIVE_UNIT;
static size_t PIN_MACRO_INDEX = 0;
static std::chrono::steady_clock::time_point PIN_MACRO_DEADLINE;

static bool pin_macro_log_hook(void *, const std::string &data, logger::Style, std::string &) {
    for (int unit = 0; unit < 2; unit++) {
        if (!AUTO_PIN_MACRO_PLAYER_ACTIVE[unit]) {
            continue;
        }
        if (data.find(AUTO_PIN_MACRO_TRIGGER[unit]) != std::string::npos) {
            AUTO_PIN_MACRO_REQUEST[unit].store(true);
        }
    }
    return false;
}

bool eamuse_get_card(int active_count, int unit_id, uint8_t *card) {

    // get unit index
    int index = unit_id > 0 && active_count > 1 ? 1 : 0;

    // check cards cached for auto-insert
    // explicitly not logging anything in this path to avoid log spam
    if (AUTO_INSERT_CARD[index] && AUTO_INSERT_CARD_CACHED[index]) {
        memcpy(card, AUTO_INSERT_CARD_CACHED_DATA[index], 8);
        return true;
    }

    // reader card input
    if (CARD_INSERT_UID_ENABLE[index]) {
        CARD_INSERT_UID_ENABLE[index] = false;
        memcpy(card, CARD_INSERT_UID[index], 8);
        log_info("eamuse", "[P{}] Inserted card from reader: {}", index+1, bin2hex(card, 8));
        return true;
    }

    // get file path
    std::filesystem::path path;
    if (!KEYPAD_BINDINGS.card_paths[index].empty()) {
        path = KEYPAD_BINDINGS.card_paths[index];
    } else {
        path = index > 0 ? "card1.txt" : "card0.txt";
    }

    // call the next function
    return eamuse_get_card(path, card, index);
}

bool eamuse_get_card(const std::filesystem::path &path, uint8_t *card, int index) {
    // do a quick copy under lock
    std::unique_lock<std::mutex> lock(CARD_OVERRIDES_LOCK);
    const auto card_override = CARD_OVERRIDES[index];
    lock.unlock();

    // Check if card overrides are present
    if (!card_override.empty()) {

        // Override is present
        for (int n = 0; n < 16; n++) {
            char c = card_override.c_str()[n];
            bool digit = c >= '0' && c <= '9';
            bool character_big = c >= 'A' && c <= 'F';
            bool character_small = c >= 'a' && c <= 'f';
            if (!digit && !character_big && !character_small) {
                log_warning("eamuse",
                            "{} card override contains an invalid character sequence at byte {} (16 characters, 0-9/A-F only)",
                            card_override, n);

                overlay::notifications::add_throttled(
                    overlay::notifications::Severity::Error,
                    fmt::format("eamuse.card_override_error.p{}", index + 1),
                    NOTIFICATION_THROTTLE_SECONDS,
                    fmt::format("[P{}] invalid card override", index + 1));

                return false;
            }
        }
        // Log info
        log_info("eamuse", "[P{}] Inserted card override: {}", index+1, card_override);

        // Card is valid, convert and set it.
        hex2bin(card_override.c_str(), card);

        // cache it for auto-insert
        // always overwrite from overrides since override may have changed by user in card manager
        if (AUTO_INSERT_CARD[index]) {
            memcpy(AUTO_INSERT_CARD_CACHED_DATA[index], card, 8);
            AUTO_INSERT_CARD_CACHED[index] = true;
            log_info("eamuse", "[P{}] Auto card insert - caching this card override in memory: {}", index+1, card_override);
        }

        // success
        return true;
    }
    // Overrides are not present. Use the standard file reading method.
    return eamuse_get_card_from_file(path, card, index);
}

bool eamuse_get_card_from_file(const std::filesystem::path &path, uint8_t *card, int index) {

    // open file
    std::ifstream f(path);
    if (!f) {
        log_warning("eamuse", "{} can not be opened!", path);
        overlay::notifications::add_throttled(
            overlay::notifications::Severity::Error,
            fmt::format("eamuse.card_file_error.p{}", index + 1),
            NOTIFICATION_THROTTLE_SECONDS,
            fmt::format("[P{}] can't open card file", index + 1));
        return false;
    }

    // get size
    f.seekg(0, f.end);
    auto length = (size_t) f.tellg();
    f.seekg(0, f.beg);

    // check size
    if (length < 16) {
        log_warning("eamuse", "{} is too small (must be at least 16 characters)", path);
        overlay::notifications::add_throttled(
            overlay::notifications::Severity::Error,
            fmt::format("eamuse.card_file_error.p{}", index + 1),
            NOTIFICATION_THROTTLE_SECONDS,
            fmt::format("[P{}] card file error", index + 1));

        return false;
    }

    // read file
    char buffer[17];
    f.read(buffer, 16);
    buffer[16] = 0;

    // verify card
    for (int n = 0; n < 16; n++) {
        char c = buffer[n];
        bool digit = c >= '0' && c <= '9';
        bool character_big = c >= 'A' && c <= 'F';
        bool character_small = c >= 'a' && c <= 'f';
        if (!digit && !character_big && !character_small) {
            log_warning("eamuse",
                "{} contains an invalid character sequence at byte {} (16 characters, 0-9/A-F only)",
                path, n);

            overlay::notifications::add_throttled(
                overlay::notifications::Severity::Error,
                fmt::format("eamuse.card_file_error.p{}", index + 1),
                NOTIFICATION_THROTTLE_SECONDS,
                fmt::format("[P{}] card file error", index + 1));
            return false;
        }
    }

    // info
    log_info("eamuse", "[P{}] Inserted {}: {}", index+1, path, buffer);

    // convert hex to bytes
    hex2bin(buffer, card);

    // cache it for auto-insert
    if (AUTO_INSERT_CARD[index] && !AUTO_INSERT_CARD_CACHED[index]) {
        memcpy(AUTO_INSERT_CARD_CACHED_DATA[index], card, 8);
        AUTO_INSERT_CARD_CACHED[index] = true;
        log_info("eamuse", "[P{}] Auto card insert - caching this card from file in memory: {}", index+1, buffer);
    }

    // success
    return true;
}

void eamuse_card_insert(int unit) {
    CARD_INSERT[unit] = true;
    CARD_INSERT_TIME[unit] = get_performance_seconds();
}

void eamuse_card_insert(int unit, const uint8_t *card) {
    memcpy(CARD_INSERT_UID[unit], card, 8);
    CARD_INSERT[unit] = true;
    CARD_INSERT_TIME[unit] = get_performance_seconds();
    CARD_INSERT_UID_ENABLE[unit] = true;
}

bool eamuse_card_insert_consume(int active_count, int unit_id, bool reader_available) {

    // get unit index
    int index = unit_id > 0 && active_count > 1 ? 1 : 0;
    const auto insert_action = unit_id > 0
        ? hotkeys::Action::InsertCardP2
        : hotkeys::Action::InsertCardP1;
    // drain manual edges before early returns so they never replay later
    const bool manual_insert = hotkeys::consume(insert_action);
    if (!reader_available) {
        return false;
    }

    // bt5api
    if (BT5API_ENABLED) {
        // some bt5api want to be polled in order.
        if (eamuse_get_game_keypads() > 1) {
            bt5api_poll_reader_card(1);
            bt5api_poll_reader_card(0);
        }
        else {
            bt5api_poll_reader_card((uint8_t) index);
        }
    }

    // auto insert card
    if (AUTO_INSERT_CARD[index]) {
        // reset timer if enough time has passed since last insert
        if (CARD_INSERT[index] &&
            AUTO_INSERT_CARD_COOLDOWN < (get_performance_seconds() - CARD_INSERT_TIME[index])) {
            CARD_INSERT[index] = false;
        }

        if (!CARD_INSERT[index]) {
            eamuse_card_insert(index);
            // not logging anything here to prevent spam
            return true;
        } else {
            return false;
        }
    }

    // check for card insert
    if ((CARD_INSERT[index] && fabs(get_performance_seconds() - CARD_INSERT_TIME[index]) < CARD_INSERT_TIMEOUT)
        || manual_insert) {

        log_info("eamuse", "[P{}] Card insert on reader (total active count: {})", unit_id+1, active_count);
        CARD_INSERT[index] = false;

        overlay::notifications::add_throttled(
            overlay::notifications::Severity::Info,
            fmt::format("eamuse.card_inserted.p{}", unit_id + 1),
            NOTIFICATION_THROTTLE_SECONDS,
            fmt::format("[P{}] card inserted", unit_id + 1));

        return true;
    }

    return false;
}

bool eamuse_coin_get_block() {
    return COIN_BLOCK;
}

void eamuse_coin_set_block(bool block) {
    COIN_BLOCK = block;
}

int eamuse_coin_get_stock() {
    return COIN_STOCK;
}

void eamuse_coin_set_stock(int amount) {
    COIN_STOCK = amount;
}

bool eamuse_coin_consume(int amount) {
    if (COIN_STOCK < amount) {
        return false;
    } else {
        COIN_STOCK -= amount;
        return true;
    }
}

int eamuse_coin_consume_stock() {
    int stock = COIN_STOCK;
    COIN_STOCK = 0;
    return stock;
}

int eamuse_coin_add() {
    return ++COIN_STOCK;
}

void eamuse_coin_insert() {
    if (COIN_BLOCK) {
        log_info("eamuse", "coin inserted while blocked");
    } else {
        log_info("eamuse", "coin insert");
        COIN_STOCK++;
    }
}

void eamuse_pin_macro_start() {
    // a unit is eligible for auto-trigger only if all the static prerequisites are
    // satisfied at startup; precomputing this lets the log hook skip per-line checks
    // on values that never change at runtime
    for (int unit = 0; unit < 2; unit++) {
        AUTO_PIN_MACRO_PLAYER_ACTIVE[unit] =
            !AUTO_PIN_MACRO_TRIGGER[unit].empty() &&
            !PIN_MACRO_VALUES[unit].empty();
        if(!AUTO_PIN_MACRO_TRIGGER[unit].empty() && PIN_MACRO_VALUES[unit].empty()) {
            log_warning("eamuse", "Configuration error; Pin Macro empty for P{}.", unit+1);
        }
    }

    // register scene log hook so the macro fires on the per-game trigger string,
    // but only if at least one player is eligible
    if (AUTO_PIN_MACRO_PLAYER_ACTIVE[0] || AUTO_PIN_MACRO_PLAYER_ACTIVE[1]) {
        logger::hook_add(pin_macro_log_hook, nullptr);
        log_info("eamuse", "AUTO_PIN_MACRO enabled");
    }

    std::lock_guard<std::mutex> lock(PIN_MACRO_MUTEX);
    PIN_MACRO_PHASE = PinMacroPhase::Idle;
    PIN_MACRO_ACTIVE_UNIT = std::nullopt;
    PIN_MACRO_INDEX = 0;
    PIN_MACRO_ACTIVE.store(true, std::memory_order_release);
}

bool eamuse_pin_macro_is_active(size_t unit) {
    return unit < std::size(PIN_MACRO_VALUES) &&
        PIN_MACRO_ACTIVE.load(std::memory_order_acquire) &&
        !PIN_MACRO_VALUES[unit].empty();
}

void eamuse_pin_macro_tick() {
    static constexpr uint16_t KEYPAD_OVERRIDES[] = {
        1 << EAM_IO_KEYPAD_0,
        1 << EAM_IO_KEYPAD_1,
        1 << EAM_IO_KEYPAD_2,
        1 << EAM_IO_KEYPAD_3,
        1 << EAM_IO_KEYPAD_4,
        1 << EAM_IO_KEYPAD_5,
        1 << EAM_IO_KEYPAD_6,
        1 << EAM_IO_KEYPAD_7,
        1 << EAM_IO_KEYPAD_8,
        1 << EAM_IO_KEYPAD_9,
    };

    if (!PIN_MACRO_ACTIVE.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(PIN_MACRO_MUTEX);
    if (!PIN_MACRO_ACTIVE.load(std::memory_order_acquire)) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (PIN_MACRO_PHASE != PinMacroPhase::Idle && now < PIN_MACRO_DEADLINE) {
        return;
    }

    if (PIN_MACRO_PHASE == PinMacroPhase::KeyDown) {
        eamuse_set_keypad_overrides(PIN_MACRO_ACTIVE_UNIT.value(), 0);
        PIN_MACRO_PHASE = PinMacroPhase::InterDigit;
        PIN_MACRO_DEADLINE = now + std::chrono::milliseconds(50);
        return;
    }

    if (PIN_MACRO_PHASE == PinMacroPhase::InterDigit &&
        PIN_MACRO_INDEX == PIN_MACRO_VALUES[PIN_MACRO_ACTIVE_UNIT.value()].length()) {
        PIN_MACRO_PHASE = PinMacroPhase::Cooldown;
        PIN_MACRO_DEADLINE = now + std::chrono::milliseconds(120);
        return;
    }

    if (PIN_MACRO_PHASE == PinMacroPhase::Cooldown) {
        PIN_MACRO_ACTIVE_UNIT = std::nullopt;
        PIN_MACRO_PHASE = PinMacroPhase::Idle;
    }

    // an idle macro consumes at most one player's trigger; other queued edges remain
    // pending until the current PIN has completed.
    if (PIN_MACRO_PHASE == PinMacroPhase::Idle) {
        for (size_t unit = 0; unit < 2; unit++) {
            if (PIN_MACRO_VALUES[unit].empty()) {
                continue;
            }
            const auto pin_button = unit == 0
                ? games::OverlayButtons::TriggerPinMacroP1
                : games::OverlayButtons::TriggerPinMacroP2;
            const bool key_press = hotkeys::consume_overlay_button(pin_button);
            const bool auto_request = AUTO_PIN_MACRO_REQUEST[unit].exchange(false);
            if (auto_request) {
                log_info("eamuse", "AUTO_PIN_MACRO_REQUEST detected for P{}", unit + 1);
            }
            if (key_press || auto_request) {
                overlay::notifications::add(
                        overlay::notifications::Severity::Info,
                        fmt::format("[P{}] PIN macro fired ({})",
                                unit + 1, auto_request ? "auto" : "manual"));
                PIN_MACRO_ACTIVE_UNIT = unit;
                PIN_MACRO_INDEX = 0;
                break;
            }
        }
    }

    if (!PIN_MACRO_ACTIVE_UNIT.has_value()) {
        return;
    }

    const auto unit = PIN_MACRO_ACTIVE_UNIT.value();
    const char pin_char = PIN_MACRO_VALUES[unit].at(PIN_MACRO_INDEX++);
    if (pin_char >= '0' && pin_char <= '9') {
        eamuse_set_keypad_overrides(unit, KEYPAD_OVERRIDES[pin_char - '0']);
    }
    PIN_MACRO_PHASE = PinMacroPhase::KeyDown;
    PIN_MACRO_DEADLINE = now + std::chrono::milliseconds(100);
}

void eamuse_pin_macro_stop() {
    {
        std::lock_guard<std::mutex> lock(PIN_MACRO_MUTEX);
        PIN_MACRO_ACTIVE.store(false, std::memory_order_release);
        if (PIN_MACRO_ACTIVE_UNIT.has_value()) {
            eamuse_set_keypad_overrides(PIN_MACRO_ACTIVE_UNIT.value(), 0);
        }
        PIN_MACRO_ACTIVE_UNIT = std::nullopt;
        PIN_MACRO_PHASE = PinMacroPhase::Idle;
    }
    if (AUTO_PIN_MACRO_PLAYER_ACTIVE[0] || AUTO_PIN_MACRO_PLAYER_ACTIVE[1]) {
        logger::hook_remove(pin_macro_log_hook, nullptr);
    }
}

void eamuse_set_keypad_overrides(size_t unit, uint16_t keypad_state) {

    // check unit
    if (unit >= std::size(KEYPAD_STATE_OVERRIDES)) {
        return;
    }

    // set state
    KEYPAD_STATE_OVERRIDES[unit] = keypad_state;
}

void eamuse_set_keypad_overrides_bt5(size_t unit, uint16_t keypad_state) {

    // check unit
    if (unit >= std::size(KEYPAD_STATE_OVERRIDES_BT5)) {
        return;
    }

    // set state
    KEYPAD_STATE_OVERRIDES_BT5[unit] = keypad_state;
}

void eamuse_set_keypad_overrides_reader(size_t unit, uint16_t keypad_state) {

    // check unit
    if (unit >= std::size(KEYPAD_STATE_OVERRIDES_READER)) {
        return;
    }

    // set state
    KEYPAD_STATE_OVERRIDES_READER[unit] = keypad_state;
}

void eamuse_set_keypad_overrides_overlay(size_t unit, uint16_t keypad_state) {

    // check unit
    if (unit >= std::size(KEYPAD_STATE_OVERRIDES_OVERLAY)) {
        return;
    }

    // set state
    KEYPAD_STATE_OVERRIDES_OVERLAY[unit] = keypad_state;
}

uint16_t eamuse_get_keypad_state(size_t unit) {

    // check unit
    if (unit >= std::size(KEYPAD_STATE)) {
        return 0;
    }

    // reset
    KEYPAD_STATE[unit] = KEYPAD_STATE_OVERRIDES[unit];
    KEYPAD_STATE[unit] |= KEYPAD_STATE_OVERRIDES_BT5[unit];
    KEYPAD_STATE[unit] |= KEYPAD_STATE_OVERRIDES_READER[unit];
    KEYPAD_STATE[unit] |= KEYPAD_STATE_OVERRIDES_OVERLAY[unit];

    // bt5api
    if (BT5API_ENABLED) {
        bt5api_poll_reader_keypad((uint8_t) unit);
    }

    // get keybinds and get offset for unit
    auto keypad_buttons = games::get_buttons_keypads(eamuse_get_game());
    auto offset = unit * games::KeypadButtons::Size;

    // parse
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad0 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_0;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad1 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_1;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad2 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_2;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad3 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_3;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad4 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_4;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad5 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_5;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad6 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_6;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad7 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_7;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad8 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_8;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad9 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_9;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::Keypad00 + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_00;
    }
    if (GameAPI::Buttons::getState(RI_MGR, keypad_buttons->at(games::KeypadButtons::KeypadDecimal + offset))) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_KEYPAD_DECIMAL;
    }
    const auto insert_action = unit == 0
        ? hotkeys::Action::InsertCardP1
        : hotkeys::Action::InsertCardP2;
    if (hotkeys::pressed(insert_action)) {
        KEYPAD_STATE[unit] |= 1 << EAM_IO_INSERT;
    }

    // return state
    return KEYPAD_STATE[unit];
}

std::string eamuse_get_keypad_state_str(size_t unit) {
    auto state = eamuse_get_keypad_state(unit);
    std::ostringstream ss;
    if (state & 1 << EAM_IO_KEYPAD_0) ss << "0";
    if (state & 1 << EAM_IO_KEYPAD_1) ss << "1";
    if (state & 1 << EAM_IO_KEYPAD_2) ss << "2";
    if (state & 1 << EAM_IO_KEYPAD_3) ss << "3";
    if (state & 1 << EAM_IO_KEYPAD_4) ss << "4";
    if (state & 1 << EAM_IO_KEYPAD_5) ss << "5";
    if (state & 1 << EAM_IO_KEYPAD_6) ss << "6";
    if (state & 1 << EAM_IO_KEYPAD_7) ss << "7";
    if (state & 1 << EAM_IO_KEYPAD_8) ss << "8";
    if (state & 1 << EAM_IO_KEYPAD_9) ss << "9";
    if (state & 1 << EAM_IO_KEYPAD_00) ss << "00";
    if (state & 1 << EAM_IO_KEYPAD_DECIMAL) ss << "00";
    if (state & 1 << EAM_IO_INSERT) ss << "00";
    return ss.str();
}

bool eamuse_keypad_state_naive() {
    return KEYPAD_BINDINGS.keypads[0].empty() && KEYPAD_BINDINGS.keypads[1].empty();
}

void eamuse_set_game(std::string game) {
    if (EAMUSE_GAME_NAME != game) {
        EAMUSE_GAME_NAME = std::move(game);
        eamuse_update_keypad_bindings();
    }
}

void eamuse_update_keypad_bindings() {
    KEYPAD_BINDINGS = Config::getInstance().getKeypadBindings(EAMUSE_GAME_NAME);
}

const std::string &eamuse_get_game() {
    return EAMUSE_GAME_NAME;
}

int eamuse_get_game_keypads() {
    if (avs::game::is_model("JDZ") ||
        avs::game::is_model("KDZ") ||
        avs::game::is_model("LDJ") ||
        avs::game::is_model("JDX") ||
        avs::game::is_model("KDX") ||
        avs::game::is_model("MDX") ||
        avs::game::is_model("J33") ||
        avs::game::is_model("K33") ||
        avs::game::is_model("L33") ||
        avs::game::is_model("M32"))
    {
        return 2;
    }

    return 1;
}

int eamuse_get_game_keypads_name() {
    auto game_name = eamuse_get_game();

    if (game_name == "Beatmania IIDX" ||
        game_name == "Dance Dance Revolution" ||
        game_name == "GitaDora")
    {
        return 2;
    }

    return 1;
}

void eamuse_autodetect_game() {
    if (avs::game::is_model("KFC"))
        eamuse_set_game("Sound Voltex");
    else if (avs::game::is_model("JDZ") ||
             avs::game::is_model("KDZ") ||
             avs::game::is_model("LDJ"))
        eamuse_set_game("Beatmania IIDX");
    else if (avs::game::is_model("J44") ||
             avs::game::is_model("K44") ||
             avs::game::is_model("L44") ||
             avs::game::is_model("T44"))
        eamuse_set_game("Jubeat");
    else if (avs::game::is_model("KDM"))
        eamuse_set_game("Dance Evolution");
    else if (avs::game::is_model("NBT"))
        eamuse_set_game("Beatstream");
    else if (avs::game::is_model("I36"))
        eamuse_set_game("Metal Gear");
    else if (avs::game::is_model("KBR") ||
             avs::game::is_model("LBR") ||
             avs::game::is_model("MBR"))
        eamuse_set_game("Reflec Beat");
    else if (avs::game::is_model("KBI"))
        eamuse_set_game("Tenkaichi Shogikai");
    else if (avs::game::is_model("K39") ||
             avs::game::is_model("L39") ||
             avs::game::is_model("M39"))
        eamuse_set_game("Pop'n Music");
    else if (avs::game::is_model("KGG"))
        eamuse_set_game("Steel Chronicle");
    else if (avs::game::is_model("JGT"))
        eamuse_set_game("Road Fighters 3D");
    else if (avs::game::is_model("PIX"))
        eamuse_set_game("Museca");
    else if (avs::game::is_model("R66"))
        eamuse_set_game("Bishi Bashi Channel");
    else if (avs::game::is_model("J32") ||
             avs::game::is_model("J33") ||
             avs::game::is_model("K32") ||
             avs::game::is_model("K33") ||
             avs::game::is_model("L32") ||
             avs::game::is_model("L33") ||
             avs::game::is_model("M32"))
        eamuse_set_game("GitaDora");
    else if (avs::game::is_model("JDX") ||
             avs::game::is_model("KDX") ||
             avs::game::is_model("MDX"))
        eamuse_set_game("Dance Dance Revolution");
    else if (avs::game::is_model("PAN"))
        eamuse_set_game("Nostalgia");
    else if (avs::game::is_model("JMA") ||
             avs::game::is_model("KMA") ||
             avs::game::is_model("LMA"))
        eamuse_set_game("Quiz Magic Academy");
    else if (avs::game::is_model("MMD"))
        eamuse_set_game("FutureTomTom");
    else if (avs::game::is_model("KK9"))
        eamuse_set_game("Mahjong Fight Club");
    else if (avs::game::is_model("JMP"))
        eamuse_set_game("HELLO! Pop'n Music");
    else if (avs::game::is_model("KLP"))
        eamuse_set_game("LovePlus");
    else if (avs::game::is_model("NSC"))
        eamuse_set_game("Scotto");
    else if (avs::game::is_model("REC"))
        eamuse_set_game("DANCERUSH");
    else if (avs::game::is_model("KCK") ||
             avs::game::is_model("NCK"))
        eamuse_set_game("Winning Eleven");
    else if (avs::game::is_model("NCG"))
        eamuse_set_game("Otoca D'or");
    else if (avs::game::is_model("LA9"))
        eamuse_set_game("Charge Machine");
    else if (avs::game::is_model("JC9"))
        eamuse_set_game("Ongaku Paradise");
    else if (avs::game::is_model("TBS"))
        eamuse_set_game("Busou Shinki: Armored Princess Battle Conductor");
    else if (avs::game::is_model("UJK"))
        eamuse_set_game("Chase Chase Jokers");
    else if (avs::game::is_model("UKS"))
        eamuse_set_game("QuizKnock STADIUM");
    else if (avs::game::is_model("VFG"))
        eamuse_set_game("Mahjong Fight Girl");
    else if (avs::game::is_model("XIF"))
        eamuse_set_game("Polaris Chord");
    else {
        log_warning("eamuse", "unknown game model: {}", avs::game::MODEL);
        eamuse_set_game("unknown");
    }
}

void eamuse_scard_callback(uint8_t slot_no, card_info_t *cardinfo) {
    log_misc("scard", "eamuse_scard_callback: slot_no={}, card_type={}", slot_no, cardinfo->card_type);
    if (cardinfo->card_type > 0) {
        eamuse_card_insert(slot_no & 1, cardinfo->uid);
    }
}

bool eamuse_scanned_card_peek_noninvasive(size_t unit, char card_uid[8]) {
    const int unit_index = unit > 0 ? 1 : 0;
    if (!CARD_INSERT_UID_ENABLE[unit_index]) {
        return false;
    }
    memcpy(card_uid, CARD_INSERT_UID[unit_index], 8);
    return true;
}

void eamuse_scanned_card_clear(size_t unit) {
    const int unit_index = unit > 0 ? 1 : 0;
    CARD_INSERT_UID_ENABLE[unit_index] = false;
}
