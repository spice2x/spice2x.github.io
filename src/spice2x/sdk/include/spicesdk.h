#pragma once
#ifndef SPICE_SDK_H
#define SPICE_SDK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SPICE_SDK_STATUS_CODE {
    SPICE_SDK_STATUS_SUCCESS = 0,

    // 000: generic
    SPICE_SDK_STATUS_GENERIC_ERROR = 1,
    SPICE_SDK_STATUS_NOT_INITIALIZED = 2,
    SPICE_SDK_STATUS_NOT_SUPPORTED = 3,
    SPICE_SDK_STATUS_TOO_SMALL = 4,

    // 1000: invalid args
    SPICE_SDK_STATUS_INVALID_ARGUMENT_1 = 1001,
    SPICE_SDK_STATUS_INVALID_ARGUMENT_2 = 1002,
    SPICE_SDK_STATUS_INVALID_ARGUMENT_3 = 1003,
    SPICE_SDK_STATUS_INVALID_ARGUMENT_4 = 1004,
    SPICE_SDK_STATUS_INVALID_ARGUMENT_5 = 1005,

} SPICE_SDK_STATUS_CODE;

typedef enum SPICE_SDK_LOG_LEVEL {
    SPICE_SDK_LOG_LEVEL_MISC = 0,
    SPICE_SDK_LOG_LEVEL_INFO = 1,
    SPICE_SDK_LOG_LEVEL_WARNING = 2,
    SPICE_SDK_LOG_LEVEL_FATAL = 3,
} SPICE_SDK_LOG_LEVEL;

typedef struct SPICE_SDK_TOUCH_POINT {
    uint32_t id;
    int x;
    int y;
} SPICE_SDK_TOUCH_POINT;

typedef struct SPICE_SDK_GAME_INFO {
    char name[64]; // null-terminated
} SPICE_SDK_GAME_INFO;

typedef struct SPICE_SDK_AVS_INFO {
    char model[4]; // "MDX", null-terminated
    char dest;     // J
    char spec;     // A
    char rev;      // A
    char ext[11];  // "2025061002", null-terminated
} SPICE_SDK_AVS_INFO;

// get_game_info (v0.1 and up)
//
// get info about the currently running game
//
//   info: receives game info

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_game_info_func)(
    SPICE_SDK_GAME_INFO *info
);

// get_avs_info (v0.1 and up)
//
// get AVS info (model, dest, spec, rev, ext)
//
//   info: receives AVS info

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_avs_info_func)(
    SPICE_SDK_AVS_INFO *info
);

// log (v0.1 and up)
// logs a message to the log
// writing a FATAL message will terminate spice, only use in catastrophic failure
// 
//   level: see log level enum
//   module: short string that identifies the facility / module / submodule
//   message: the message to log

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_log_func)(
    SPICE_SDK_LOG_LEVEL level,
    const char *module,
    const char *message
);

// set_button (v0.1 and up)
// sets or clears the button override
//
// make sure to hold the button long enough for the game's I/O engine to pick up
// usually, one or two frames
//
//   button_id: ID of the button; see spicesdk_io.h for named values
//   pressed: true to set the button override (permanently set the button to be ON until cleared),
//            false to clear the override (allow user's controller to provide input again)
//   velocity: MIDI velocity of the button; only valid when pressed is true
//             can be between 0.0 and 1.0, inclusive

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_set_button_func)(
    uint32_t button_id,
    bool pressed,
    float velocity
);

// set_analog (v0.1 and up)
// sets or clears the analog override
//
//   analog_id: ID of the analog; see spicesdk_io.h for named values
//   override_active: true to set override (gain exclusive control)
//                    false to clear it (allow user's controller provide input again)
//   value: value of the analog; only valid when override_active is true
//          can be between 0.0 and 1.0, inclusive

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_set_analog_func)(
    uint32_t analog_id,
    bool override_active,
    float value
);

// get_light (v0.1 and up)
// gets the last observed value of a light
//
//   light_id: ID of the light; see spicesdk_io.h for named values
//   light_value: output parameter for the light value; 0.0 to 1.0

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_light_func)(
    uint32_t light_id,
    float *light_value
);

// set_touch (v0.1 and up)
// adds or updates touch points
//
//   points: array of touch points to add or update
//   count: number of touch points in the array

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_set_touch_func)(
    const SPICE_SDK_TOUCH_POINT *points,
    uint32_t count
);

// clear_touch (v0.1 and up)
// clears touch points (i.e., no longer being touched)
//
//   ids: array of touch point IDs to clear

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_clear_touch_func)(
    const uint32_t *ids,
    uint32_t count
);

// insert_card (v0.1 and up)
// simulates inserting an e-amuse card with the given ID
//
//   unit: 0 for player 1, 1 for player 2
//   card_id: null-terminated string of the card ID

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_insert_card_func)(
    uint8_t unit,
    const char *card_id
);

// set_keypad (v0.1 and up)
// sets keypad state
//
// make sure to hold the button long enough for the game to pick up
// 70ms is usually sufficient, except for DDR which needs 150ms
//
//   unit: 0 for player 1, 1 for player 2
//   key: '0' to '9' for numbers, 'A' for 00, 'D' for decimal point, 0 or '\0' to release all keys

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_set_keypad_func)(
    uint8_t unit,
    char key
);

typedef struct SPICE_SDK_V0 {
    uint32_t size;

    spice_sdk_log_func *log;

    spice_sdk_get_game_info_func *get_game_info;
    spice_sdk_get_avs_info_func *get_avs_info;

    spice_sdk_set_button_func *set_button;
    spice_sdk_set_analog_func *set_analog;
    spice_sdk_get_light_func *get_light;

    spice_sdk_set_touch_func *set_touch;
    spice_sdk_clear_touch_func *clear_touch;

    spice_sdk_insert_card_func *insert_card;
    spice_sdk_set_keypad_func *set_keypad;

} SPICE_SDK_V0;

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_init_func)(
    uint32_t version,
    void *functions
);

typedef int (__cdecl *spice_sdk_entry_point_t)(
    spice_sdk_init_func *init
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SPICE_SDK_H