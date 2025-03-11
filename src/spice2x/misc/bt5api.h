#pragma once

#include <cstdint>
#include <windows.h>

/* Emulating the sensors of a slotted card reader. The reader has one
   sensor at the front that detects if a card is getting inserted or
   if the card is not fully removed. When the back sensor is triggered
   the card is locked in the slot and its data is read. */
enum eam_io_sensor_state {
    EAM_IO_SENSOR_FRONT = 0,
    EAM_IO_SENSOR_BACK = 1,
};

/* Different commands for the (slotted) reader. The game triggers one
   of these actions and the card slot as to execute it. When non-slotted
   readers are emulated, these states are not used/set. */
enum eam_io_card_slot_cmd {
    EAM_IO_CARD_SLOT_CMD_CLOSE = 0,
    EAM_IO_CARD_SLOT_CMD_OPEN = 1,
    EAM_IO_CARD_SLOT_CMD_EJECT = 2,
    EAM_IO_CARD_SLOT_CMD_READ = 3,
};

/* A private function pointer table returned by the stock EAMIO.DLL
   implementation and consumed by config.exe. The contents of this table are
   undocumented and subject to change without notice. */
struct eam_io_config_api;

extern bool BT5API_ENABLED;

void bt5api_init();
void bt5api_hook(HINSTANCE module);
void bt5api_poll_reader_card(uint8_t unit_no);
void bt5api_poll_reader_keypad(uint8_t unit_no);
void bt5api_dispose();
