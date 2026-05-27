#include <vector>
#include <mutex>
#include <shared_mutex>

#include "sdk.h"
#include "avs/game.h"
#include "games/io.h"
#include "launcher/launcher.h"
#include "misc/eamuse.h"
#include "overlay/notifications.h"
#include "sdk/include/spicesdk.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/utils.h"
#include "acio/mdxf/mdxf_poll.h"

namespace sdk {

static spice_sdk_init_func sdk_init;
static spice_sdk_log_func sdk_log;
static spice_sdk_get_avs_info_func sdk_get_avs_info;
static spice_sdk_get_game_info_func sdk_get_game_info;
static spice_sdk_get_button_func sdk_get_button;
static spice_sdk_set_button_func sdk_set_button;
static spice_sdk_get_analog_func sdk_get_analog;
static spice_sdk_set_analog_func sdk_set_analog;
static spice_sdk_get_light_func sdk_get_light;
static spice_sdk_set_light_func sdk_set_light;
static spice_sdk_set_touch_func sdk_set_touch;
static spice_sdk_clear_touch_func sdk_clear_touch;
static spice_sdk_insert_card_func sdk_insert_card;
static spice_sdk_set_keypad_func sdk_set_keypad;
static spice_sdk_add_toast_func sdk_add_toast;

struct SdkModule {
    std::string dll;
    HINSTANCE module;
};

// DLLs
static int sdk_modules_count = 0;
static std::vector<SdkModule> sdk_modules_list;
static std::shared_mutex sdk_global_mutex;

// internal
static bool sdk_initialized = false;
static bool sdk_shutting_down = false;
static std::vector<Button> *buttons;
static std::vector<Analog> *analogs;
static std::vector<Light> *lights;

// callbacks
static std::mutex sdk_callback_registration_mutex;
static std::vector<spice_sdk_destroy_callback_func *> callbacks_destroy;

void register_sdk_hooks(std::string dll, HINSTANCE module) {
    sdk_modules_list.emplace_back(std::move(dll), module);
    sdk_modules_count += 1;
}

void init_sdk_modules() {
    if (sdk_modules_count == 0) {
        return;
    }

    // prepare
    buttons = games::get_buttons(eamuse_get_game());
    analogs = games::get_analogs(eamuse_get_game());
    lights = games::get_lights(eamuse_get_game());

    // under exclusive lock, mark the SDK helpers as being available
    {
        std::unique_lock lock(sdk_global_mutex);
        sdk_initialized = true;
    }

    // without any locks, call into each DLL; this may call back into SDK functions
    for (auto &module : sdk_modules_list) {
        // get a pointer to dll's spice_sdk_entry_point
        const auto entry_point = reinterpret_cast<spice_sdk_entry_point_func *>(
            GetProcAddress(module.module, "spice_sdk_entry_point"));
        if (!entry_point) {
            continue;
        }

        // call into it on this thread
        log_info("sdk", "registering SDK hooks for {}", module.dll);
        const auto ret = entry_point(sdk_init);
        log_info("sdk", "spice_sdk_entry_point returned {}", ret);
    }
}

void fini_sdk_modules() {
    // prevent multiple calls and further calls into sdk_init
    {
        std::unique_lock lock(sdk_global_mutex);
        if (!sdk_initialized) {
            return;
        }
        sdk_shutting_down = true;
    }

    // call into destroy callback of each DLL
    // this may call back into SDK functions (e.g., for logging)
    // so we leave sdk_initialized as-is
    {
        log_info("sdk", "calling destroy callbacks...");
        std::lock_guard lock(sdk_callback_registration_mutex);
        for (const auto& destroy : callbacks_destroy) {
            destroy();
        }
    }

    // under exclusive lock, mark the SDK functions as no longer available
    {
        std::unique_lock lock(sdk_global_mutex);
        sdk_initialized = false;
    }
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_init(
    uint32_t version,
    spice_sdk_destroy_callback_func *destroy_callback,
    void *sdk_functions
)  
{
    std::shared_lock lock(sdk_global_mutex);
    if (sdk_shutting_down || !sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    uint32_t size;
    if (version != 0) {
        log_warning("sdk", "sdk_init returning NOT_SUPPORTED due to invalid version: {}", version);
        return SPICE_SDK_STATUS_NOT_SUPPORTED;
    }

    if (!destroy_callback) {
        log_warning("sdk", "sdk_init returning INVALID_ARGUMENT_2 due to invalid destroy_callback pointer");
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }
    if (!sdk_functions) {
        log_warning("sdk", "sdk_init returning INVALID_ARGUMENT_3 due to invalid sdk_functions pointer");
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_3;
    }

    auto *v0 = reinterpret_cast<SPICE_SDK_V0 *>(sdk_functions);
    if (v0->size < RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, set_keypad)) {
        log_warning("sdk", "sdk_init returning TOO_SMALL due to size field of SPICE_SDK_V0 not being set");
        return SPICE_SDK_STATUS_TOO_SMALL;
    }

    // we are trusting the size passed in by the caller
    size = v0->size;
    memset(v0, 0, size);
    v0->size = size;

    v0->log = sdk_log;
    v0->get_game_info = sdk_get_game_info;
    v0->get_avs_info = sdk_get_avs_info;
    v0->get_button = sdk_get_button;
    v0->set_button = sdk_set_button;
    v0->get_analog = sdk_get_analog;
    v0->set_analog = sdk_set_analog;
    v0->get_light = sdk_get_light;
    v0->set_light = sdk_set_light;
    v0->set_touch = sdk_set_touch;
    v0->clear_touch = sdk_clear_touch;
    v0->insert_card = sdk_insert_card;
    v0->set_keypad = sdk_set_keypad;
    // end of 0.1

    if (v0->size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, add_toast)) {
        v0->add_toast = sdk_add_toast;
    }

    // end of 0.2
    // any newer minor iterations will need to check the size

    {
        // destroy callbacks are only called upon successful return from this routine
        std::lock_guard lock(sdk_callback_registration_mutex);
        callbacks_destroy.push_back(destroy_callback);
    }

    log_info("sdk", "sdk_init returning SUCCESS");
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_log(
    SPICE_SDK_LOG_LEVEL level,
    const char *facility,
    const char *message
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (facility == nullptr) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    const std::string facility_str = fmt::format("sdk::{}", facility);

    switch (level) {
        case SPICE_SDK_LOG_LEVEL_MISC:
            log_misc(facility_str.c_str(), "{}", message);
            break;
        case SPICE_SDK_LOG_LEVEL_INFO:
            log_info(facility_str.c_str(), "{}", message);
            break;
        case SPICE_SDK_LOG_LEVEL_WARNING:
            log_warning(facility_str.c_str(), "{}", message);
            break;
        case SPICE_SDK_LOG_LEVEL_FATAL:
            log_fatal(facility_str.c_str(), "{}", message);
            break;
        default:
            return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_get_avs_info(
    SPICE_SDK_AVS_INFO *info
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!info) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    // MDXJAA2025061002
    strncpy(info->model, avs::game::MODEL, sizeof(info->model));
    info->dest = avs::game::DEST[0];
    info->spec = avs::game::SPEC[0];
    info->rev = avs::game::REV[0];
    strncpy(info->ext, avs::game::EXT, sizeof(info->ext));
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_get_game_info(
    SPICE_SDK_GAME_INFO *info
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!info) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    *info = {};

    const std::string game_name = eamuse_get_game();
    strncpy(info->name, game_name.c_str(), sizeof(info->name));
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_get_button (
    uint32_t button_id,
    bool *pressed,
    float *velocity
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!buttons) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (button_id >= buttons->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    Button &button = (*buttons)[button_id];
    if (pressed) {
        *pressed = (GameAPI::Buttons::getState(RI_MGR, button) == GameAPI::Buttons::BUTTON_PRESSED);
    }
    if (velocity) {
        *velocity = GameAPI::Buttons::getVelocity(RI_MGR, button);
    }

    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_set_button (
    uint32_t button_id,
    bool pressed,
    float velocity
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!buttons) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (button_id >= buttons->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (pressed) {
        velocity = std::clamp(velocity, 0.f, 1.f);
    }

    Button &button = (*buttons)[button_id];
    if (pressed) {
        button.override_state = pressed ? GameAPI::Buttons::BUTTON_PRESSED : GameAPI::Buttons::BUTTON_NOT_PRESSED;
        button.override_velocity = velocity;
    }
    button.override_enabled = pressed;
    mdxf_poll(true);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_get_analog (
    uint32_t analog_id,
    float *value
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!analogs) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (analog_id >= analogs->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (!value) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    Analog &analog = (*analogs)[analog_id];
    *value = GameAPI::Analogs::getState(RI_MGR, analog);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_set_analog (
    uint32_t analog_id,
    bool override_active,
    float value
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!analogs) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (analog_id >= analogs->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (override_active) {
        value = std::clamp(value, 0.f, 1.f);
    }

    Analog &analog = (*analogs)[analog_id];
    if (override_active) {
        analog.override_state = value;
    }
    analog.override_enabled = override_active;
    mdxf_poll(true);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_get_light(
    uint32_t light_id,
    float *value
    )
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!lights) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (light_id >= lights->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (!value) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }
    Light &light = (*lights)[light_id];
    *value = GameAPI::Lights::readLight(RI_MGR, light);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_set_light(
    uint32_t light_id,
    bool override_active,
    float value
    )
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!lights) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (light_id >= lights->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (override_active) {
        value = std::clamp(value, 0.f, 1.f);
    }

    Light &light = (*lights)[light_id];
    if (override_active) {
        light.override_state = value;
    }
    light.override_enabled = override_active;
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_set_touch(
    const SPICE_SDK_TOUCH_POINT *points,
    uint32_t count
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (count == 0 || !points) {
        return SPICE_SDK_STATUS_TOO_SMALL;
    }

    std::vector<TouchPoint> touch_points;
    for (uint32_t i = 0; i < count; i++) {
        const SPICE_SDK_TOUCH_POINT &point = points[i];
        touch_points.emplace_back(TouchPoint {
            .id = point.id,
            .x = point.x,
            .y = point.y,
            .mouse = false,
        });
    }
    touch_write_points(&touch_points);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_clear_touch(
    const uint32_t *ids,
    uint32_t count
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (count == 0 || !ids) {
        return SPICE_SDK_STATUS_TOO_SMALL;
    }

    std::vector<DWORD> touch_point_ids;
    for (uint32_t i = 0; i < count; i++) {
        touch_point_ids.push_back(ids[i]);
    }
    touch_remove_points(&touch_point_ids);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_insert_card(
    uint8_t unit,
    const char *card_id
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (unit >= 2) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    if (!card_id) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }
    if (strlen(card_id) != 16) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    // convert to binary
    uint8_t card_bin[8] {};
    if (!hex2bin(card_id, card_bin)) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    // insert
    eamuse_card_insert(unit & 1, card_bin);
    return SPICE_SDK_STATUS_SUCCESS;
}

struct KeypadMapping {
    char character;
    uint16_t state;
};

static KeypadMapping KEYPAD_MAPPINGS[] = {
    { '0', 1 << EAM_IO_KEYPAD_0 },
    { '1', 1 << EAM_IO_KEYPAD_1 },
    { '2', 1 << EAM_IO_KEYPAD_2 },
    { '3', 1 << EAM_IO_KEYPAD_3 },
    { '4', 1 << EAM_IO_KEYPAD_4 },
    { '5', 1 << EAM_IO_KEYPAD_5 },
    { '6', 1 << EAM_IO_KEYPAD_6 },
    { '7', 1 << EAM_IO_KEYPAD_7 },
    { '8', 1 << EAM_IO_KEYPAD_8 },
    { '9', 1 << EAM_IO_KEYPAD_9 },
    { 'A', 1 << EAM_IO_KEYPAD_00 },
    { 'D', 1 << EAM_IO_KEYPAD_DECIMAL },
};

SPICE_SDK_STATUS_CODE
__cdecl
sdk_set_keypad(
    uint8_t unit,
    char key
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (unit >= 2) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    if (key == 0) {
        eamuse_set_keypad_overrides(unit, 0);
        return SPICE_SDK_STATUS_SUCCESS;
    }

    // find mapping
    uint16_t state = 0;
    bool mapping_found = false;
    for (auto &mapping : KEYPAD_MAPPINGS) {
        if (mapping.character == key) {
            state |= mapping.state;
            mapping_found = true;
            break;
        }
    }

    // check for error
    if (!mapping_found) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    // set
    eamuse_set_keypad_overrides(unit, state);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_add_toast(
    SPICE_SDK_TOAST_SEVERITY severity,
    const char *text
)
{
    std::shared_lock lock(sdk_global_mutex);
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }

    if (!text) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    overlay::notifications::Severity sev;
    switch (severity) {
        case SPICE_SDK_TOAST_LEVEL_INFO:
            sev = overlay::notifications::Severity::Info;
            break;
        case SPICE_SDK_TOAST_LEVEL_SUCCESS:
            sev = overlay::notifications::Severity::Success;
            break;
        case SPICE_SDK_TOAST_LEVEL_WARNING:
            sev = overlay::notifications::Severity::Warning;
            break;
        case SPICE_SDK_TOAST_LEVEL_ERROR:
            sev = overlay::notifications::Severity::Error;
            break;
        default:
            return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    overlay::notifications::add(sev, text);
    return SPICE_SDK_STATUS_SUCCESS;
}


} // namespace sdk