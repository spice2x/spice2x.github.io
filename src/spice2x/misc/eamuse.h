#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>

#include "external/scard/scard.h"

/* From BT5 API for compatibility reasons.
   Scan codes for the so-called "10 key" button panel on each card reader. Each
   scan code corresponds to a bit position within the 16-bit bitfield that you
   return from eam_io_get_keypad_state(). */
enum eam_io_keypad_scan_code {
    EAM_IO_KEYPAD_0        = 0,
    EAM_IO_KEYPAD_1        = 1,
    EAM_IO_KEYPAD_4        = 2,
    EAM_IO_KEYPAD_7        = 3,
    EAM_IO_KEYPAD_00       = 4,
    EAM_IO_KEYPAD_2        = 5,
    EAM_IO_KEYPAD_5        = 6,
    EAM_IO_KEYPAD_8        = 7,
    EAM_IO_KEYPAD_DECIMAL  = 8,
    EAM_IO_KEYPAD_3        = 9,
    EAM_IO_KEYPAD_6        = 10,
    EAM_IO_KEYPAD_9        = 11,

    EAM_IO_KEYPAD_COUNT    = 12, /* Not an actual scan code */

    EAM_IO_INSERT = 13, /* SpiceTools Extension */
};

extern std::string CARD_OVERRIDES[2];
extern bool AUTO_INSERT_CARD[2];
extern float AUTO_INSERT_CARD_COOLDOWN;

extern bool PIN_MACRO_ENABLED;
extern std::string PIN_MACRO_VALUES[2];

bool eamuse_get_card(int active_count, int unit_id, uint8_t *card);
bool eamuse_get_card(const std::filesystem::path &path, uint8_t *card, int unit_id);
bool eamuse_get_card_from_file(const std::filesystem::path &path, uint8_t *card, int index);

void eamuse_card_insert(int unit);
void eamuse_card_insert(int unit, const uint8_t *card);

bool eamuse_card_insert_consume(int active_count, int unit_id);

bool eamuse_coin_get_block();
void eamuse_coin_set_block(bool block);

int eamuse_coin_get_stock();
void eamuse_coin_set_stock(int amount);
bool eamuse_coin_consume(int amount);
int eamuse_coin_consume_stock();

int eamuse_coin_add();

void eamuse_coin_start_thread();
void eamuse_coin_stop_thread();

void eamuse_pin_macro_start_thread();
void eamuse_pin_macro_stop_thread();

void eamuse_set_keypad_overrides(size_t unit, uint16_t keypad_state);
void eamuse_set_keypad_overrides_bt5(size_t unit, uint16_t keypad_state);
void eamuse_set_keypad_overrides_reader(size_t unit, uint16_t keypad_state);
void eamuse_set_keypad_overrides_overlay(size_t unit, uint16_t keypad_state);

uint16_t eamuse_get_keypad_state(size_t unit);
std::string eamuse_get_keypad_state_str(size_t unit);
void eamuse_update_keypad_bindings();

bool eamuse_keypad_state_naive();

void eamuse_set_game(std::string game);

std::string eamuse_get_game();
int eamuse_get_game_keypads();
int eamuse_get_game_keypads_name();

void eamuse_autodetect_game();

void eamuse_scard_callback(uint8_t slot_no, card_info_t *cardinfo);

bool eamuse_scanned_card_peek_noninvasive(size_t unit, char card_uid[8]);
void eamuse_scanned_card_clear(size_t unit);
