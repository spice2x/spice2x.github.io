#include <string.h>
#include <stdio.h>
#include <windows.h>
#include <process.h>

#include "sdk/include/spicesdk.h"
#include "sdk/include/spicesdk_io.h"

static SPICE_SDK_V0 spice;

static spice_sdk_destroy_callback_func destroy_callback;

static void test_logging();
static void test_game_info();
static void test_avs_info();

static void get_buttons();
static void set_buttons();
static void clear_buttons();

static void get_analogs();
static void set_analogs();
static void clear_analogs();

static void get_lights();
static void set_lights();
static void clear_lights();

static void set_touch();
static void clear_touch();
static void insert_card();
static void set_keypad();
static void clear_keypad();

static HANDLE worker_stop_event;
static HANDLE worker_handle;
static unsigned __stdcall worker_thread(void *arg);

// this sample assumes that the game is IIDX, but it doesn't check for it.

__declspec(dllexport)
int
__cdecl
spice_sdk_entry_point(
    spice_sdk_init_func *init
)
{
    SPICE_SDK_STATUS_CODE status;

    memset(&spice, 0, sizeof(spice));
    spice.size = sizeof(spice);
    status = init(0, destroy_callback, &spice);
    if (status != SPICE_SDK_STATUS_SUCCESS) {
        return 0;
    }

    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "plugin loaded");
    test_logging();
    test_game_info();
    test_avs_info();

    worker_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!worker_stop_event) {
        spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", "failed to create worker stop event");
        return 0;
    }

    worker_handle = (HANDLE)_beginthreadex(
        NULL,          // security
        0,             // stack size
        worker_thread, // function
        NULL,          // argument
        0,             // flags
        NULL           // thread id
    );
    if (!worker_handle) {
        spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", "failed to create worker thread");
        CloseHandle(worker_stop_event);
        worker_stop_event = NULL;
        return 0;
    }

    return 1;
}

void
__cdecl
destroy_callback(
    void
)
{
    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "plugin unloaded");
    if (worker_stop_event) {
        SetEvent(worker_stop_event);
    }
    if (worker_handle) {
        WaitForSingleObject(worker_handle, INFINITE);
        CloseHandle(worker_handle);
        worker_handle = NULL;
    }
    if (worker_stop_event) {
        CloseHandle(worker_stop_event);
        worker_stop_event = NULL;
    }
}

static unsigned __stdcall worker_thread(void *arg) {
    int phase = 0;
    while (WaitForSingleObject(worker_stop_event, 0) == WAIT_TIMEOUT) {
        phase += 1;
        switch (phase) {
            case 1:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "get buttons...");
                get_buttons();
                break;
            case 2:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "set buttons...");
                set_buttons();
                break;
            case 3:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "clear buttons...");
                clear_buttons();
                break;
            case 4:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "get analogs...");
                get_analogs();
                break;
            case 5:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "set analogs...");
                set_analogs();
                break;
            case 6:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "clear analogs...");
                clear_analogs();
                break;
            case 7:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "get lights...");
                get_lights();
                break;
            case 8:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "set lights...");
                set_lights();
                break;
            case 9:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "clear lights...");
                clear_lights();
                break;
            case 10:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "set touch...");
                set_touch();
                break;
            case 11:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "clear touch...");
                clear_touch();
                break;
            case 12:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "insert card...");
                insert_card();
                break;
            case 13:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "set keypad...");
                set_keypad();
                break;
            case 14:
                spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "clear keypad...");
                clear_keypad();
                break;
            default:
                phase = 0;
                break;
        }
        if (phase != 0) {
            WaitForSingleObject(worker_stop_event, 3000);
        }
    }
    return 0;
}

static void test_logging() {
    spice.log(SPICE_SDK_LOG_LEVEL_MISC, "sample_v0", "this is a misc message");
    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", "this is an info message");
    spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", "this is a warning message");
}

static void test_game_info() {
    SPICE_SDK_GAME_INFO info;
    SPICE_SDK_STATUS_CODE status;

    status = spice.get_game_info(&info);
    if (status != SPICE_SDK_STATUS_SUCCESS) {
        spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", "get_game_info failed");
        return;
    }
    char log_message[128];
    snprintf(log_message, sizeof(log_message), "game info - name: %s", info.name);
    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", log_message);
}

static void test_avs_info() {
    SPICE_SDK_AVS_INFO info;
    SPICE_SDK_STATUS_CODE status;

    status = spice.get_avs_info(&info);
    if (status != SPICE_SDK_STATUS_SUCCESS) {
        spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", "get_avs_info failed");
        return;
    }

    char log_message[128];
    snprintf(
        log_message,
        sizeof(log_message),
        "avs - model: %s, dest: %c, spec: %c, rev: %c, ext: %s",
        info.model, info.dest, info.spec, info.rev, info.ext);

    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", log_message);
}

static void get_buttons() {
    bool pressed;
    float velocity;
    spice.get_button(IIDX_Button_P1_Headphone, &pressed, &velocity);
    
    char log_message[128];
    snprintf(
        log_message,
        sizeof(log_message),
        "button P1_Headphone pressed: %s, velocity: %.2f",
        pressed ? "ON" : "off", velocity);

    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", log_message);
}

static void set_buttons() {
    spice.set_button(IIDX_Button_P1_1, true, 0.3f);
    spice.set_button(IIDX_Button_P1_3, true, 0.5f);
    spice.set_button(IIDX_Button_P1_5, true, 0.7f);
}

static void clear_buttons() {
    spice.set_button(IIDX_Button_P1_1, false, 0.f);
    spice.set_button(IIDX_Button_P1_3, false, 0.f);
    spice.set_button(IIDX_Button_P1_5, false, 0.f);
}

static void get_analogs() {
    float value;
    spice.get_analog(IIDX_Analog_TT_P1, &value);
    
    char log_message[128];
    snprintf(
        log_message,
        sizeof(log_message),
        "analog TT_P1: %.2f",
        value);

    spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", log_message);
}

static void set_analogs() {
    spice.set_analog(IIDX_Analog_TT_P1, true, 0.25f);
    spice.set_analog(IIDX_Analog_TT_P2, true, 0.75f);
}

static void clear_analogs() {
    spice.set_analog(IIDX_Analog_TT_P1, false, 0.f);
    spice.set_analog(IIDX_Analog_TT_P2, false, 0.f);
}

static void get_lights() {
    float value;
    SPICE_SDK_STATUS_CODE status;

    status = spice.get_light(IIDX_Light_TT_P1_Resistance, &value);
    if (status == SPICE_SDK_STATUS_SUCCESS) {
        char log_message[64];
        snprintf(log_message, sizeof(log_message), "P1 TT resistance value: %.2f", value);
        spice.log(SPICE_SDK_LOG_LEVEL_INFO, "sample_v0", log_message);
    } else {
        spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", "get_light failed");
    }
}

static void set_lights() {
    spice.set_light(IIDX_Light_P1_Start, true, 1.f);
}

static void clear_lights() {
    spice.set_light(IIDX_Light_P1_Start, false, 0.f);
}

static void set_touch() {
    SPICE_SDK_TOUCH_POINT points[2] = {
        { .id = 1, .x = 100, .y = 200 },
        { .id = 2, .x = 300, .y = 400 },
    };
    spice.set_touch(points, 2);
}

static void clear_touch() {
    uint32_t ids[2] = { 1, 2 };
    spice.clear_touch(ids, 2);
}

static void insert_card() {
    spice.insert_card(0, "E004010000001234");
}

static void set_keypad() {
    SPICE_SDK_STATUS_CODE ret = spice.set_keypad(0, '3');
    if (ret != SPICE_SDK_STATUS_SUCCESS) {
        char log_message[64];
        snprintf(log_message, sizeof(log_message), "set_keypad failed: %d", ret);
        spice.log(SPICE_SDK_LOG_LEVEL_WARNING, "sample_v0", log_message);
    }
}

static void clear_keypad() {
    spice.set_keypad(0, 0);
}
