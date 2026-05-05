#include <vector>
#include <mutex>

#include "sdk.h"
#include "avs/game.h"
#include "games/io.h"
#include "launcher/launcher.h"
#include "misc/eamuse.h"
#include "sdk/include/spicesdk.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/utils.h"

namespace sdk {

static spice_sdk_init_func sdk_init;
static spice_sdk_log_func sdk_log;
static spice_sdk_get_avs_info_func sdk_get_avs_info;
static spice_sdk_get_game_info_func sdk_get_game_info;
static spice_sdk_set_button_func sdk_set_button;
static spice_sdk_set_analog_func sdk_set_analog;
static spice_sdk_get_light_func sdk_get_light;
static spice_sdk_set_touch_func sdk_set_touch;
static spice_sdk_clear_touch_func sdk_clear_touch;
static spice_sdk_insert_card_func sdk_insert_card;
static spice_sdk_set_keypad_func sdk_set_keypad;

struct SdkModule {
    std::string dll;
    HINSTANCE module;
};

// DLLs
static int sdk_modules_count = 0;
static std::vector<SdkModule> sdk_modules_list;
static std::mutex sdk_modules_mutex;

// internal
static bool sdk_initialized = false;
static std::vector<Button> *buttons;
static std::vector<Analog> *analogs;
static std::vector<Light> *lights;

void register_sdk_hooks(std::string dll, HINSTANCE module) {
    std::lock_guard lock(sdk_modules_mutex);
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
    sdk_initialized = true;

    // call into DLLs
    std::lock_guard lock(sdk_modules_mutex);
    for (auto &module : sdk_modules_list) {
        // get a pointer to dll's spice_sdk_entry_point
        const auto entry_point = reinterpret_cast<spice_sdk_entry_point_t>(
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
    sdk_initialized = false;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_init(
    uint32_t version,
    void *functions
)  
{
    uint32_t size;
    if (version != 0) {
        return SPICE_SDK_STATUS_NOT_SUPPORTED;
    }

    auto *v0 = reinterpret_cast<SPICE_SDK_V0 *>(functions);
    if (v0->size < RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, size)) {
        return SPICE_SDK_STATUS_TOO_SMALL;
    }
    
    size = v0->size;
    memset(v0, 0, size);
    v0->size = size;

    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, log)) {
        v0->log = sdk_log;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, get_game_info)) {
        v0->get_game_info = sdk_get_game_info;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, get_avs_info)) {
        v0->get_avs_info = sdk_get_avs_info;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, set_button)) {
        v0->set_button = sdk_set_button;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, set_analog)) {
        v0->set_analog = sdk_set_analog;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, get_light)) {
        v0->get_light = sdk_get_light;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, set_touch)) {
        v0->set_touch = sdk_set_touch;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, clear_touch)) {
        v0->clear_touch = sdk_clear_touch;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, insert_card)) {
        v0->insert_card = sdk_insert_card;
    }
    if (size >= RTL_SIZEOF_THROUGH_FIELD(SPICE_SDK_V0, set_keypad)) {
        v0->set_keypad = sdk_set_keypad;
    }

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
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
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
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
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
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
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
sdk_set_button (
    uint32_t button_id,
    bool pressed,
    float velocity
)
{
    if (!buttons || !sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (button_id >= buttons->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (velocity < 0.f || velocity > 1.f) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_3;
    }

    Button &button = (*buttons)[button_id];
    if (pressed) {
        button.override_state = pressed ? GameAPI::Buttons::BUTTON_PRESSED : GameAPI::Buttons::BUTTON_NOT_PRESSED;
        button.override_velocity = velocity;
    }
    button.override_enabled = pressed;
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
    if (!analogs || !sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (analog_id >= analogs->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (value < 0.f || value > 1.f) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }

    Analog &analog = (*analogs)[analog_id];
    if (override_active) {
        analog.override_state = value;
    }
    analog.override_enabled = override_active;
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_get_light(
    uint32_t light_id,
    float *light_value
    )
{
    if (!lights || !sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }
    if (light_id >= lights->size()) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (!light_value) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }
    Light &light = (*lights)[light_id];
    *light_value = GameAPI::Lights::readLight(RI_MGR, light);
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE
__cdecl
sdk_set_touch(
    const SPICE_SDK_TOUCH_POINT *points,
    uint32_t count
)
{
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
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
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
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
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }

    if (unit > 2) {
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
    if (!sdk_initialized) {
        return SPICE_SDK_STATUS_NOT_INITIALIZED;
    }

    if (unit > 2) {
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


} // namespace sdk