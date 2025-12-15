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

// These are used to determine if a thread needs to be spun up to keep pad state ring buffers populated with enough recent polls
static uint64_t START_TIME = 0;
static int CALL_COUNT = 0;
static int THRESHOLD_REFRESH_RATE = 120;
static bool IS_REFRESH_RATE_DETERMINED = false;
static bool IS_THREAD_NEEDED = false;

static std::atomic<bool> MDXF_THREAD_RUNNING{false};
static std::thread MDXF_THREAD;

static constexpr int THREAD_REFRESH_RATE_HZ = 125;
static constexpr auto THREAD_PERIOD = std::chrono::milliseconds(1000 / THREAD_REFRESH_RATE_HZ);

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
    INTERNAL_POLL = 1,
    EXTERNAL_POLL = 2
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

// Used to keep the ring buffer populated with steady updates. 60Hz interval is too slow
static void mdxf_thread_start() {
    bool expected = false;
    if (!MDXF_THREAD_RUNNING.compare_exchange_strong(expected, true)) {
        return;
    }

    MDXF_THREAD = std::thread([] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        while (MDXF_THREAD_RUNNING.load(std::memory_order_acquire)) {
            mdxf_poll(false);
            std::this_thread::sleep_for(THREAD_PERIOD);
        }
    });
}

static void mdxf_thread_stop() {
    if (!MDXF_THREAD_RUNNING.exchange(false)) {
        return;
    }

    if (MDXF_THREAD.joinable()) {
        MDXF_THREAD.join();
    }

}

// Snaps measured refresh rate to best fit
static int snap_refresh_rate(int measured_hz) {
    static constexpr std::array<int, 6> rates = {
        60, 120, 144, 165, 180, 240
    };

    int best = rates[0];
    int best_err = std::fabs(measured_hz - best);

    for (int r : rates) {
        int err = std::fabs(measured_hz - r);
        if (err < best_err) {
            best = r;
            best_err = err;
        }
    }
    return best;
}

// Increments the number of times the update function was called, then calculates the current refresh rate of the game after 3 seconds has passed
static void count_calls_from_game() {
    if (IS_REFRESH_RATE_DETERMINED) {
        return;
    }
    
    uint64_t current_time = arkGetTickTime64();
    
    if (START_TIME == 0) {
        START_TIME = current_time;
    }
    
    CALL_COUNT++;
    
    uint64_t elapsed_time = current_time - START_TIME;
    if (elapsed_time >= 3000) {
        double measured_hz = static_cast<double>(CALL_COUNT) * 1000.0 / static_cast<double>(elapsed_time);
        
        // Account for the main loop calling this twice per iteration
        measured_hz *= 0.5;
        int snapped_hz = snap_refresh_rate(static_cast<int>(measured_hz));

        IS_THREAD_NEEDED = (snapped_hz < THRESHOLD_REFRESH_RATE);
        IS_REFRESH_RATE_DETERMINED = true;

        log_info("ARKMDXP4", "Detected: {}Hz, Best Fit: {}Hz (Needs 125Hz Helper Thread: {})", static_cast<int>(measured_hz), snapped_hz, IS_THREAD_NEEDED ? "Yes" : "No");

        if (IS_THREAD_NEEDED) {
            mdxf_thread_start();
        }
    }
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

static bool __cdecl ac_io_mdxf_update_control_status_buffer_impl(int node, MDXFPollSource source) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {
        // Marks this module as actively being used, allowing this function to be called from other sources
        if (source == ARKMDXP4_POLL) {
            if (!IS_MDXF_ACTIVE) {
                IS_MDXF_ACTIVE = true;
            }
            count_calls_from_game();
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
        
        uint16_t current_state = (uint16_t(up_down) << 8) | left_right;
        uint64_t current_time = arkGetTickTime64();
        
        std::lock_guard<std::mutex> lock(*mutex);
        
        // If state hasn't changed and the update was triggered externally, then don't advance head pointer or write a new entry
        if (source == EXTERNAL_POLL && *prev_state == current_state) {
            return true;
        }
        
        // If there's already an entry for this exact time, then don't advance head pointer or write a new entry 
        if (*prev_time == current_time) {
            return true;
        }
        
        *prev_state = current_state;
        *prev_time = current_time;

        // Advance head pointer
        *head = (*head + 1) % STATUS_BUFFER_NUM_ENTRIES;
        uint8_t* buffer_entry = buffer[*head];

        // Clear buffer
        memset(buffer_entry, 0, STATUS_BUFFER_SIZE);
        
        // Write button state
        buffer_entry[4] = up_down;
        buffer_entry[5] = left_right;
        
        //Write current game time
        *(uint64_t*)&buffer_entry[0x18] = current_time;
    }

    // return success
    return true;
}

static bool __cdecl ac_io_mdxf_update_control_status_buffer(int node) {
    return ac_io_mdxf_update_control_status_buffer_impl(node, ARKMDXP4_POLL);
}

// Used for triggering updates of the controller states from outside arkmdxp4.dll main refresh loop (i.e. within rawinput.cpp on controller events)
void mdxf_poll(bool isExternal) {
    if (IS_MDXF_ACTIVE) {
        MDXFPollSource source = isExternal ? EXTERNAL_POLL : INTERNAL_POLL;
        ac_io_mdxf_update_control_status_buffer_impl(17, source);
        ac_io_mdxf_update_control_status_buffer_impl(18, source);
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

acio::MDXFModule::~MDXFModule() {
    mdxf_thread_stop();
}