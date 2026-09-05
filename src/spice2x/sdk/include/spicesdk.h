#pragma once
#ifndef SPICE_SDK_H
#define SPICE_SDK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
#define SPICE_SDK_ENTRY_POINT extern "C" __declspec(dllexport) int __cdecl
#else
#define SPICE_SDK_ENTRY_POINT __declspec(dllexport) int __cdecl
#endif

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
    SPICE_SDK_STATUS_TOO_LATE = 5,

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

typedef enum SPICE_SDK_TOAST_SEVERITY {
    SPICE_SDK_TOAST_LEVEL_INFO = 0,
    SPICE_SDK_TOAST_LEVEL_SUCCESS = 1,
    SPICE_SDK_TOAST_LEVEL_WARNING = 2,
    SPICE_SDK_TOAST_LEVEL_ERROR = 3,
} SPICE_SDK_TOAST_SEVERITY;

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

// get_button (v0.1 and up)
// gets the button state
//
//   button_id: ID of the button; see spicesdk_io.h for named values
//   pressed: (optional) is the button pressed?
//   velocity: (optional) MIDI velocity of the button

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_button_func)(
    uint32_t button_id,
    bool *pressed,
    float *velocity
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

// get_analog (v0.1 and up)
// gets the analog state
//
//   button_id: ID of the button; see spicesdk_io.h for named values
//   value: receives the state of the analog

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_analog_func)(
    uint32_t analog_id,
    float *value
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
//   value: output parameter for the light value; 0.0 to 1.0

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_light_func)(
    uint32_t light_id,
    float *value
);

// set_light (v0.1 and up)
// sets or clears the light override
//
//   light_id: ID of the light; see spicesdk_io.h for named values
//   light_value: output parameter for the light value; 0.0 to 1.0

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_set_light_func)(
    uint32_t light_id,
    bool override_active,
    float light_value
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

// add_toast (v0.2 and up)
// adds an overlay toast notification
//
//   severity: see SPICE_SDK_TOAST_SEVERITY; controls the accent color (purely cosmetic)
//   text: null-terminated UTF-8 message to display; wraps inside the toast

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_add_toast_func)(
    SPICE_SDK_TOAST_SEVERITY severity,
    const char *text
);

// insert_coin (v0.3 and up)
// adds to the shared coin stock, bypassing the coin blocker like the Spice API
//
//   amount: number of coins to insert (0 to 255); use 1 for a single coin
//           zero succeeds without changing the stock

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_insert_coin_func)(
    uint8_t amount
);

typedef struct SPICE_SDK_MODULE_INFO {
    uint32_t size; // initialize to sizeof(SPICE_SDK_MODULE_INFO)
    uintptr_t base; // loaded address, not the preferred PE image base
    uint32_t image_size;
    uint32_t timestamp;
    uint32_t entry_point; // RVA, not an absolute address
    uint16_t machine; // PE IMAGE_FILE_MACHINE_* value
} SPICE_SDK_MODULE_INFO;

// get_module_info (v0.4 and up)
// gets PE info about an already loaded module
//
// does not load or permanently pin the module; the caller must keep it loaded
// while using the returned base address
// returns GENERIC_ERROR if not found or unreadable, NOT_SUPPORTED for non-PE32/PE32+
//
//   module_name: null-terminated UTF-16 module name or full path
//   info: receives module info; initialize size to sizeof(SPICE_SDK_MODULE_INFO)

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_module_info_func)(
    const wchar_t *module_name,
    SPICE_SDK_MODULE_INFO *info
);

// get_plugin_directory (v0.4 and up)
// gets the directory containing a registered SDK DLL
//
// available during entry-point initialization and until destroy callbacks finish
// returns INVALID_ARGUMENT_1 for addresses outside registered SDK DLLs
//
//   plugin_address: address of a function or static object in the plugin DLL
//                   used only to identify the DLL; the pointed-to object is not read
//                   address of any global variable in your DLL will work
//   buffer: caller-owned UTF-16 output, or NULL to query the required size
//   size: input capacity and output required wchar_t count, including the terminator
//         NULL buffer or insufficient capacity returns TOO_SMALL and sets the
//         required size without partial output

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_get_plugin_directory_func)(
    const void *plugin_address,
    wchar_t *buffer,
    uint32_t *size
);

typedef enum SPICE_SDK_D3D9_EVENT {
    SPICE_SDK_D3D9_READY = 0,
    SPICE_SDK_D3D9_DRAW = 1,
    SPICE_SDK_D3D9_INVALIDATE = 2,
    SPICE_SDK_D3D9_DESTROY = 3,
} SPICE_SDK_D3D9_EVENT;

typedef struct SPICE_SDK_D3D9_FRAME {
    uint32_t size;
    void *device; // borrowed IDirect3DDevice9*, not Spice's wrapper
    void *window; // HWND
    uint32_t width;
    uint32_t height;
} SPICE_SDK_D3D9_FRAME;

typedef void (__cdecl spice_sdk_d3d9_callback_func)(
    SPICE_SDK_D3D9_EVENT event,
    const SPICE_SDK_D3D9_FRAME *frame,
    void *userdata
);

// register_d3d9 (v0.4 and up)
// registers a process-lifetime renderer for the primary D3D9 presentation target
//
// READY precedes drawing on a usable device and follows each successful reset
// DRAW runs on top of all Spice overlays, even when the overlay is closed; the host
// binds the backbuffer, brackets the scene, and restores graphics state
// INVALIDATE precedes reset; release default-pool resources, even if reset fails
// DESTROY releases all device references before device destruction or SDK shutdown
// callbacks are serialized; DRAW/READY run on the presentation thread; teardown
// runs at a graphics-thread boundary, or after the game stops rendering
// do not throw, block, reset/present the
// device, or shut down Spice from a callback; frame is valid only during the call
// no callbacks run after the plugin's SDK destroy callback begins
// registration from inside a render callback is not supported
// input capture and non-D3D9 backends are not provided
//
//   callback: function in a registered plugin DLL; the DLL is retained until exit
//   userdata: opaque plugin state passed unchanged to each callback

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_register_d3d9_func)(
    spice_sdk_d3d9_callback_func *callback,
    void *userdata
);

// hook_library (v0.5 and up)
//
// maps an exact LoadLibrary name to a module that the plugin has already loaded
//
//   library_name: exact ANSI library name or path used by the target
//   module: module handle returned by LoadLibrary; keep it loaded until shutdown

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_hook_library_func)(
    const char *library_name,
    void *module
);

typedef struct SPICE_SDK_V0 {
    uint32_t size;

    spice_sdk_log_func *log;

    spice_sdk_get_game_info_func *get_game_info;
    spice_sdk_get_avs_info_func *get_avs_info;

    spice_sdk_get_button_func *get_button;
    spice_sdk_set_button_func *set_button;

    spice_sdk_get_analog_func *get_analog;
    spice_sdk_set_analog_func *set_analog;

    spice_sdk_get_light_func *get_light;
    spice_sdk_set_light_func *set_light;

    spice_sdk_set_touch_func *set_touch;
    spice_sdk_clear_touch_func *clear_touch;

    spice_sdk_insert_card_func *insert_card;
    spice_sdk_set_keypad_func *set_keypad;

    spice_sdk_add_toast_func *add_toast;

    spice_sdk_insert_coin_func *insert_coin;

    spice_sdk_get_module_info_func *get_module_info;
    spice_sdk_get_plugin_directory_func *get_plugin_directory;

    spice_sdk_register_d3d9_func *register_d3d9;

    spice_sdk_hook_library_func *hook_library;

} SPICE_SDK_V0;

typedef void (__cdecl spice_sdk_destroy_callback_func)(
    void
);

// init (v0.1 and up)
//
//   version: supply 0
//   destroy_callback: supply a function pointer that will be called when spice
//                     is shutting down
//   sdk_functions: supply a pointer to SPICE_SDK_V0; ensure size field is initialized
//                  to sizeof(SPICE_SDK_V0) before calling this function

typedef SPICE_SDK_STATUS_CODE (__cdecl spice_sdk_init_func)(
    uint32_t version,
    spice_sdk_destroy_callback_func *destroy_callback,
    void *sdk_functions
);

typedef int (__cdecl spice_sdk_entry_point_func)(
    spice_sdk_init_func *init
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SPICE_SDK_H
