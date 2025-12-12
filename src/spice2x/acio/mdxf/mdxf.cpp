#include "mdxf.h"

#include "avs/game.h"
#include "games/ddr/io.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

// constants
const size_t STATUS_BUFFER_SIZE = 32;
const size_t STATUS_BUFFER_NUM_ENTRIES = 16;

// static stuff
static uint8_t HEAD_P1 = 0;
static uint8_t HEAD_P2 = 0;

static std::mutex MUTEX_P1;
static std::mutex MUTEX_P2;

// Fast-poll thread state
static std::atomic<bool> MDXF_POLL_RUNNING{false};
static std::thread MDXF_POLL_THREAD;

static constexpr int MAX_POLLING_RATE = 2000;

//The polling thread will use more CPU with the limit removed. This could probably be a spicecfg option.
static const bool REMOVE_POLLING_RATE_LIMIT = false;

// buffers
#pragma pack(push, 1)
static struct {
    uint8_t STATUS_BUFFER_P1[STATUS_BUFFER_NUM_ENTRIES][STATUS_BUFFER_SIZE] {};
    uint8_t STATUS_BUFFER_P2[STATUS_BUFFER_NUM_ENTRIES][STATUS_BUFFER_SIZE] {};
} BUFFERS {};
#pragma pack(pop)

static bool STATUS_BUFFER_FREEZE = false;

typedef uint64_t (__cdecl *ARK_GET_TICK_TIME64_T)();

static uint64_t arkGetTickTime64() {
    static ARK_GET_TICK_TIME64_T getTickTime64 =
        (ARK_GET_TICK_TIME64_T)GetProcAddress(avs::game::DLL_INSTANCE, "arkGetTickTime64");
    if (getTickTime64 == nullptr) {
        // this works on 32-bit versions of avs, but not on 64.
        // it's better than nothing though.
        return timeGetTime();
    }
    return getTickTime64();
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

static bool __cdecl ac_io_mdxf_update_control_status_buffer(int node) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }
	
	uint8_t *buffer_entry = nullptr;
	uint8_t (*buffer)[STATUS_BUFFER_SIZE];
	uint8_t *head = nullptr;
	std::mutex* mutex = nullptr;
	
	switch (node) {
		case 17:
		case 25:
			mutex = &MUTEX_P1;
			head = &HEAD_P1;
			buffer = BUFFERS.STATUS_BUFFER_P1;
			break;
		case 18:
		case 26:
			mutex = &MUTEX_P2;
			head = &HEAD_P2;
			buffer = BUFFERS.STATUS_BUFFER_P2;
			break;
		default:
			// return failure on unknown node
			return false;
	}
	
	std::lock_guard<std::mutex> lock(*mutex);
	
	buffer_entry = buffer[*head];
	uint64_t current_time = *(uint64_t*)&buffer_entry[0x18];
	uint64_t now = arkGetTickTime64();
	
	// If there's already an entry for this exact moment, then don't advance head pointer or write a new entry
	if (current_time == now) {
		return true;
	}
	
	// Advance head pointer
	*head = (*head + 1) % STATUS_BUFFER_NUM_ENTRIES;
	buffer_entry = buffer[*head];
	
	// Clear buffer
	memset(buffer_entry, 0, STATUS_BUFFER_SIZE);

    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {
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

		*(uint64_t*)&buffer_entry[0x18] = now;

        // get buttons
        auto &buttons = games::ddr::get_buttons();

        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[0]))) {
            buffer_entry[4] |= 0xF0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[1]))) {
            buffer_entry[4] |= 0x0F;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[2]))) {
            buffer_entry[5] |= 0xF0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[3]))) {
            buffer_entry[5] |= 0x0F;
        }
    }

    // return success
    return true;
}

static void mdxf_poll() {
    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {
        // Update P1 + P2 MDXF nodes at faster cadence
		ac_io_mdxf_update_control_status_buffer(17);
		ac_io_mdxf_update_control_status_buffer(18);
	} 
}

// Start the poll background thread (idempotent)
static void mdxf_poll_thread_start() {
    bool expected = false;
    if (!MDXF_POLL_RUNNING.compare_exchange_strong(expected, true)) {
        // Already running
        return;
    }
	
	// Request high-resolution timer (1 ms)
    timeBeginPeriod(1);

    MDXF_POLL_THREAD = std::thread([] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		
		using clock = std::chrono::steady_clock;
        const auto period = std::chrono::microseconds(1'000'000 / MAX_POLLING_RATE);
        auto next_wake = clock::now() + period;

        while (MDXF_POLL_RUNNING.load(std::memory_order_acquire)) {
			mdxf_poll();
			if (!REMOVE_POLLING_RATE_LIMIT) {
				next_wake += period;
				auto now = clock::now();
				// 1) Sleep most of the gap
				auto sleep_until_time = next_wake - std::chrono::microseconds(200);
				if (sleep_until_time > now) {
					std::this_thread::sleep_until(sleep_until_time);
				}
				// 2) Spin for the last ~200 µs
				while (clock::now() < next_wake) {
					// low-impact spin
					_mm_pause();
				}
			}
		}
    });
}

// Stop the poll thread (idempotent)
static void mdxf_poll_thread_stop() {
    bool was_running = MDXF_POLL_RUNNING.exchange(false);
    if (!was_running) {
        return;
    }

    if (MDXF_POLL_THREAD.joinable()) {
        MDXF_POLL_THREAD.join();
    }
	
	// Release the high-resolution timer request
    timeEndPeriod(1);
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
	
	// Start background polling for DDR
    mdxf_poll_thread_start();
}

acio::MDXFModule::~MDXFModule() {
    // Make sure the polling thread is shut down cleanly
    mdxf_poll_thread_stop();
}