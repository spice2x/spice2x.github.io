#include "mdxf.h"
#include "mdxf_poll.h"

#include "avs/game.h"
#include "games/ddr/io.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"
#include <mutex>

// constants
const size_t STATUS_BUFFER_SIZE = 32;
const size_t STATUS_BUFFER_NUM_ENTRIES = 16;

// static stuff
static uint8_t HEAD_P1 = 0;
static uint8_t HEAD_P2 = 0;

static uint16_t PREV_STATE_P1 = 0;
static uint16_t PREV_STATE_P2 = 0;
static uint64_t PREV_TIME_P1 = 0;
static uint64_t PREV_TIME_P2 = 0;

static std::mutex MUTEX_P1;
static std::mutex MUTEX_P2;

static bool IS_MDXF_ACTIVE = false;

static const uint8_t BACKFILL_INTERVAL_MS = 4;
static const uint8_t BACKFILL_PADDING_MS = 2;

// buffers
#pragma pack(push, 1)
static struct {
    uint8_t STATUS_BUFFER_P1[STATUS_BUFFER_NUM_ENTRIES][STATUS_BUFFER_SIZE] {};
    uint8_t STATUS_BUFFER_P2[STATUS_BUFFER_NUM_ENTRIES][STATUS_BUFFER_SIZE] {};
} BUFFERS {};
#pragma pack(pop)

static bool STATUS_BUFFER_FREEZE = false;

typedef enum {
    ARKMDXP4_POLL = 0,
    EXTERNAL_POLL = 1
} MDXFPollSource;

typedef uint64_t (__cdecl *ARK_GET_TICK_TIME64_T)();

static uint64_t arkGetTickTime64() {
    static ARK_GET_TICK_TIME64_T getTickTime64 = nullptr;

    if (!getTickTime64) {
        HMODULE h = avs::game::DLL_INSTANCE;
        if (h) {
            getTickTime64 = (ARK_GET_TICK_TIME64_T)GetProcAddress(h, "arkGetTickTime64");
        }
    }
    // this works on 32-bit versions of avs, but not on 64.
    // it's better than nothing though.
    return getTickTime64 ? getTickTime64() : timeGetTime();
}

/*
 * Implementations
 */

static uint64_t __cdecl ac_io_mdxf_get_control_status_buffer(int node, void *out, uint8_t index, uint8_t head_in) {

    // Default error value (matches original mask behavior)
    auto error_ret = static_cast<uint64_t>(node - 0x11) & 0xFFFFFFFFFFFFFF00;
    
    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {
        // Select player-specific state
        std::mutex* mutex = nullptr;
        uint8_t* head = nullptr;
        uint8_t (*buffer)[STATUS_BUFFER_SIZE];
        size_t size = STATUS_BUFFER_NUM_ENTRIES;
        
        if (node == 17 || node == 25) {
            mutex = &MUTEX_P1;
            head = &HEAD_P1;
            buffer = BUFFERS.STATUS_BUFFER_P1;
        } else if (node == 18 || node == 26) {
            mutex = &MUTEX_P2;
            head = &HEAD_P2;
            buffer = BUFFERS.STATUS_BUFFER_P2;
        } else {
            memset(out, 0, STATUS_BUFFER_SIZE);
            return error_ret;
        }
        
        std::lock_guard<std::mutex> lock(*mutex);

        if (head_in != 0xFF) {
            *head = head_in;
        }

        // Compute ring index: walk backwards from head as index increases
        // Assumes ring buffer size is a power of two
        const size_t mask = size - 1;
        const size_t offset = static_cast<size_t>(index) & mask;
        const size_t i = (static_cast<size_t>(*head) - offset + size) & mask;

        // Copy the chosen entry
        memcpy(out, buffer[i], STATUS_BUFFER_SIZE);

        // Return the head value actually used
        return static_cast<uint64_t>(*head);
    }

    return error_ret;
}

static bool __cdecl ac_io_mdxf_set_output_level(unsigned int a1, unsigned int a2, uint8_t value) {
    if (avs::game::is_model("MDX")) {
        static const struct {
            int a2[4];
        } mapping[] = {
            {
                // a1 == 17
                {
                    games::ddr::Lights::GOLD_P1_STAGE_UP_RIGHT,
                    games::ddr::Lights::GOLD_P1_STAGE_DOWN_LEFT,
                    games::ddr::Lights::GOLD_P1_STAGE_UP_LEFT,
                    games::ddr::Lights::GOLD_P1_STAGE_DOWN_RIGHT
                }
            },
            {
                // a1 == 18
                {
                    games::ddr::Lights::GOLD_P2_STAGE_UP_RIGHT,
                    games::ddr::Lights::GOLD_P2_STAGE_DOWN_LEFT,
                    games::ddr::Lights::GOLD_P2_STAGE_UP_LEFT,
                    games::ddr::Lights::GOLD_P2_STAGE_DOWN_RIGHT
                }
            }
        };
        if ((a1 == 17 || a1 == 18) && (a2 < 4)) {
            // get light from mapping
            const auto light = mapping[a1 - 17].a2[a2];

            // get lights
            auto &lights = games::ddr::get_lights();

            // write lights
            GameAPI::Lights::writeLight(RI_MGR, lights[light], value / 128.f);
        }
    }
    
    return true;
}

static bool __cdecl ac_io_mdxf_update_control_status_buffer_impl(int node, MDXFPollSource source, uint64_t current_time) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {
        // Marks this module as actively being used, allowing this function to be called from other sources
        if (!IS_MDXF_ACTIVE && source == ARKMDXP4_POLL) {
            IS_MDXF_ACTIVE = true;
        }
        
        uint8_t (*buffer)[STATUS_BUFFER_SIZE];
        uint8_t *head = nullptr;
        uint16_t *prev_state = nullptr;
        uint64_t *prev_time = nullptr;
        std::mutex* mutex = nullptr;
        
        switch (node) {
            case 17:
            case 25:
                mutex = &MUTEX_P1;
                head = &HEAD_P1;
                prev_state = &PREV_STATE_P1;
                prev_time = &PREV_TIME_P1;
                buffer = BUFFERS.STATUS_BUFFER_P1;
                break;
            case 18:
            case 26:
                mutex = &MUTEX_P2;
                head = &HEAD_P2;
                prev_state = &PREV_STATE_P2;
                prev_time = &PREV_TIME_P2;
                buffer = BUFFERS.STATUS_BUFFER_P2;
                break;
            default:
                // return failure on unknown node
                return false;
        }
    
        // Sensor Map (LDUR):
        // FOOT DOWN = bit 32-35 = byte 4, bit 0-3
        // FOOT UP = bit 36-39 = byte 4, bit 4-7
        // FOOT RIGHT = bit 40-43 = byte 5, bit 0-3
        // FOOT LEFT = bit 44-47 = byte 5, bit 4-7
        static const size_t buttons_p1[] = {
                games::ddr::Buttons::P1_PANEL_UP,
                games::ddr::Buttons::P1_PANEL_DOWN,
                games::ddr::Buttons::P1_PANEL_LEFT,
                games::ddr::Buttons::P1_PANEL_RIGHT,
        };
        static const size_t buttons_p2[] = {
                games::ddr::Buttons::P2_PANEL_UP,
                games::ddr::Buttons::P2_PANEL_DOWN,
                games::ddr::Buttons::P2_PANEL_LEFT,
                games::ddr::Buttons::P2_PANEL_RIGHT,
        };

        // decide on button map
        const size_t *button_map = nullptr;
        switch (node) {
            case 17:
            case 25:
                button_map = &buttons_p1[0];
                break;
            case 18:
            case 26:
                button_map = &buttons_p2[0];
                break;
        }
        
        uint16_t current_state;
        if (source == EXTERNAL_POLL) {
            // get buttons
            auto &buttons = games::ddr::get_buttons();
            uint8_t up_down = 0;
            uint8_t left_right = 0;
            
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[0]))) {
                up_down |= 0xF0;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[1]))) {
                up_down |= 0x0F;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[2]))) {
                left_right |= 0xF0;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[3]))) {
                left_right |= 0x0F;
            }
            current_state = (uint16_t(up_down) << 8) | left_right;
        }
        else {
            current_state = *prev_state;
        }
        
        std::lock_guard<std::mutex> lock(*mutex);
        
        const bool has_state_changed = *prev_state != current_state;
        const bool has_time_changed = *prev_time < current_time;
        
        // If state hasn't changed and either the update was triggered externally or the time hasn't changed, then don't advance head pointer or write a new entry
        if (!has_state_changed && (source == EXTERNAL_POLL || !has_time_changed)) {
            return true;
        }
        
        // The start and stop time cutoffs for backfilling entries. Min(..) ensures times aren't negative.
        // The stop time is just before the current_time, set by BACKFILL_PADDING_MS, which avoids the last backfilled entry being too close to current_time.
        uint64_t start_time = *prev_time;
        const uint64_t stop_time = current_time - std::min<uint64_t>(current_time, BACKFILL_PADDING_MS);
        
        // Ensures the first iteration will write the first entry at current_time and not backfill to time 0ms.
        if (start_time == 0) {
            start_time = current_time - std::min<uint64_t>(current_time, BACKFILL_INTERVAL_MS);
        }
        
        // Ensures only STATUS_BUFFER_NUM_ENTRIES entries at most are backfilled
        const uint64_t max_backfill = BACKFILL_INTERVAL_MS * STATUS_BUFFER_NUM_ENTRIES;
        const uint64_t min_time = current_time - std::min<uint64_t>(current_time, max_backfill);
        if (start_time < min_time) {
            start_time = min_time;
        }
        
        // Only write one entry if called externally
        if (source == EXTERNAL_POLL) {
            start_time = stop_time - 1;
        }
        
        uint64_t time = start_time;
        uint16_t state = *prev_state;
        
        // Backfill entries a fixed interval apart from each other between prev_time and current_time
        while (time < stop_time) {
            // Advance head pointer
            *head = (*head + 1) % STATUS_BUFFER_NUM_ENTRIES;
            uint8_t* buffer_entry = buffer[*head];

            // Clear buffer
            memset(buffer_entry, 0, STATUS_BUFFER_SIZE);
            
            time += BACKFILL_INTERVAL_MS;
            
            // If the stop time is reached, then write current_time and current_state instead for this final iteration
            const bool isEdge = (time >= stop_time);
            if (isEdge) {
                state = current_state;
                time = current_time;
            }
            
            // Write button state
            buffer_entry[4] = (state >> 8) & 0xFF;
            buffer_entry[5] = state & 0xFF;
            
            // Write game time
            *(uint64_t*)&buffer_entry[0x18] = time;
        }
        
        *prev_state = current_state;
        *prev_time = current_time;
    }

    // return success
    return true;
}

static bool __cdecl ac_io_mdxf_update_control_status_buffer(int node) {
    return ac_io_mdxf_update_control_status_buffer_impl(node, ARKMDXP4_POLL, arkGetTickTime64());
}

// Used for triggering updates of the controller states from outside arkmdxp4.dll main refresh loop (i.e. within rawinput.cpp on controller events)
void mdxf_poll() {
    if (IS_MDXF_ACTIVE) {
        const uint64_t call_time_ms = arkGetTickTime64();
        ac_io_mdxf_update_control_status_buffer_impl(17, EXTERNAL_POLL, call_time_ms);
        ac_io_mdxf_update_control_status_buffer_impl(18, EXTERNAL_POLL, call_time_ms);
    }
}

/*
 * Module stuff
 */

acio::MDXFModule::MDXFModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("MDXF", module, hookMode) {
    this->status_buffer = (uint8_t*) &BUFFERS;
    this->status_buffer_size = sizeof(BUFFERS);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::MDXFModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_mdxf_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_mdxf_set_output_level);
    ACIO_MODULE_HOOK(ac_io_mdxf_update_control_status_buffer);
}