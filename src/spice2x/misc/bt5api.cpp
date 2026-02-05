#include "bt5api.h"
#include <string>
#include <thread>
#include <windows.h>
#include <external/robin_hood.h>
#include "games/iidx/iidx.h"
#include "hooks/libraryhook.h"
#include "util/logging.h"
#include "util/libutils.h"
#include "avs/game.h"
#include "eamuse.h"

#ifdef __GNUC__
/* Bemanitools is compiled with GCC (MinGW, specifically) as of version 5 */
#define LOG_CHECK_FMT __attribute__(( format(printf, 2, 3) ))
#else
/* Compile it out for MSVC plebs */
#define LOG_CHECK_FMT
#endif

/* An AVS-style logger function. Comes in four flavors: misc, info, warning,
   and fatal, with increasing severity. Fatal loggers do not return, they
   abort the running process after writing their message to the log.

   "module" is an arbitrary short string identifying the source of the log
   message. The name of the calling DLL is a good default choice for this
   string, although you might want to identify a module within your DLL here
   instead.

   "fmt" is a printf-style format string. Depending on the context in which
   your DLL is running you might end up calling a logger function exported
   from libavs, which has its own printf implementation (including a number of
   proprietary extensions), so don't use any overly exotic formats. */

typedef void (*log_formatter_t)(const char *module, const char *fmt, ...)
        LOG_CHECK_FMT;

/* An API for spawning threads. This API is defined by libavs, although
   Bemanitools itself may supply compatible implementations of these functions
   to your DLL, depending on the context in which it runs.

   NOTE: You may only use the logging functions from a thread where Bemanitools
   calls you, or a thread that you create using this API. Failure to observe
   this restriction will cause the process to crash. This is a limitation of
   libavs itself, not Bemanitools. */

typedef int (*thread_create_t)(int (*proc)(void *), void *ctx,
                               uint32_t stack_sz, unsigned int priority);
typedef void (*thread_join_t)(int thread_id, int *result);
typedef void (*thread_destroy_t)(int thread_id);

/* The first function that will be called on your DLL. You will be supplied
   with four function pointers that may be used to log messages to the game's
   log file. See comments in glue.h for further information. */
typedef void (__cdecl *eam_io_set_loggers_t)(log_formatter_t misc, log_formatter_t info,
                                               log_formatter_t warning, log_formatter_t fatal);
static eam_io_set_loggers_t eam_io_set_loggers = nullptr;


/* Initialize your card reader emulation DLL. Thread management functions are
   provided to you; you must use these functions to create your own threads if
   you want to make use of the logging functions that are provided to
   eam_io_set_loggers(). You will also need to pass these thread management
   functions on to geninput if you intend to make use of that library.

   See glue.h and geninput.h for further details. */
typedef bool (__cdecl *eam_io_init_t)(thread_create_t thread_create, thread_join_t thread_join,
                                        thread_destroy_t thread_destroy);
static eam_io_init_t eam_io_init = nullptr;

/* Shut down your card reader emulation DLL. */
typedef void (__cdecl *eam_io_fini_t)(void);
static eam_io_fini_t eam_io_fini = nullptr;

/* Return the state of the number pad on your reader. This function will be
   called frequently. See enum eam_io_keypad_scan_code above for the meaning of
   each bit within the return value.

   This function will be called even if the running game does not actually have
   a number pad on the real cabinet (e.g. Jubeat).

   unit_no is either 0 or 1. Games with only a single reader (jubeat, popn,
   drummania) will only use unit_no 0. */
typedef uint16_t (__cdecl *eam_io_get_keypad_state_t)(uint8_t unit_no);
static eam_io_get_keypad_state_t eam_io_get_keypad_state = nullptr;

/* Indicate which sensors (front and back) are triggered for a slotted reader
   (refer to enum). To emulate non-slotted readers, just set both sensors
   to on to indicate the card is in range of the reader. This function
   will be called frequently. */
typedef uint8_t (__cdecl *eam_io_get_sensor_state_t)(uint8_t unit_no);
static eam_io_get_sensor_state_t eam_io_get_sensor_state = nullptr;

/* Read a card ID. This function is only called when the return value of
   eam_io_get_sensor_state() changes from false to true, so you may take your
   time and perform file I/O etc, within reason. You must return exactly eight
   bytes into the buffer pointed to by card_id. */
typedef bool (__cdecl *eam_io_read_card_t)(uint8_t unit_no, uint8_t *card_id, uint8_t nbytes);
static eam_io_read_card_t eam_io_read_card = nullptr;

/* Send a command to the card slot. This is called by the game to execute
   certain actions on a slotted reader (refer to enum). When emulating
   wave pass readers, this is function is never called. */
typedef bool (__cdecl *eam_io_card_slot_cmd_t)(uint8_t unit_no, uint8_t cmd);
static eam_io_card_slot_cmd_t eam_io_card_slot_cmd = nullptr;

/* This function is called frequently. Update your device and states in here */
typedef bool (__cdecl *eam_io_poll_t)(uint8_t unit_no);
static eam_io_poll_t eam_io_poll = nullptr;

/* Return a pointer to an internal configuration API for use by config.exe.
   Custom implementations should return NULL. */
typedef const struct eam_io_config_api* (__cdecl *eam_io_get_config_api_t)(void);
static eam_io_get_config_api_t eam_io_get_config_api = nullptr;

bool BT5API_ENABLED = false;
static HMODULE EAMIO_DLL;
static std::string EAMIO_DLL_NAME = "eamio.dll";
static robin_hood::unordered_map<int, std::thread *> BT5API_THREADS;
static robin_hood::unordered_map<int, int> BT5API_THREAD_RESULTS;
static uint8_t BT5API_CARD_STATES[] = {0, 0};
static uint16_t BT5API_KEYPAD_STATES[] = {0, 0};

static void bt5api_log(const char *bt5_module, const char *fmt, ...) {

    // pre-compute module
    std::string module = fmt::format("bt5api:{}", bt5_module != nullptr ? bt5_module : "(null)");

    // string format
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    const auto result = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // check if format failed, fallback to logging the format string
    if (result < 0) {
        log_info(module.c_str(), "{}", fmt);
        return;
    }

    // log it if the buffer was enough
    const size_t len = (size_t) result;
    if (len < sizeof(buf)) {
        log_info(module.c_str(), "{}", buf);
    } else {

        // allocate a new string and format again
        std::string new_buf(len, '\0');
        va_start(args, fmt);
        std::vsnprintf(new_buf.data(), len + 1, fmt, args);
        va_end(args);

        // log the result
        log_info(module.c_str(), "{}", new_buf);
    }
}

static int bt5api_thread_create(int (*proc)(void *), void *ctx, uint32_t stack_sz, unsigned int priority) {
    std::thread *thread = new std::thread([proc, ctx]() {
        int thread_id = (int) std::hash<std::thread::id>{}(std::this_thread::get_id());
        BT5API_THREAD_RESULTS[thread_id] = proc(ctx);
    });

    int thread_id = static_cast<int>(std::hash<std::thread::id>{}(thread->get_id()));
    BT5API_THREADS[thread_id] = thread;

    return thread_id;
}

static void bt5api_thread_join(int thread_id, int *result) {
    BT5API_THREADS[thread_id]->join();

    if (result != nullptr) {
        *result = BT5API_THREAD_RESULTS[thread_id];
    }
}

static void bt5api_thread_destroy(int thread_id) {
    auto thread_handle = BT5API_THREADS.find(thread_id);
    if (thread_handle != BT5API_THREADS.end()) {
        if (thread_handle->second->joinable()) {
            thread_handle->second->detach();
        }
        delete thread_handle->second;
        BT5API_THREADS.erase(thread_handle);
    }
    BT5API_THREAD_RESULTS.erase(thread_id);
}

void bt5api_init() {

    // check if already initialized
    if (EAMIO_DLL) {
        return;
    }

    log_info("bt5api", "initializing");

    // load DLL instances
    EAMIO_DLL = libutils::try_library(EAMIO_DLL_NAME);
    if (!EAMIO_DLL) {
        EAMIO_DLL = libutils::try_library("..\\" + EAMIO_DLL_NAME);
    }
    if (!EAMIO_DLL) {
        log_warning("bt5api", "unable to load '{}': 0x{:x}", EAMIO_DLL_NAME, GetLastError());
        return;
    }

    // load eamio funcs
    eam_io_set_loggers = libutils::try_proc<eam_io_set_loggers_t>(EAMIO_DLL, "eam_io_set_loggers");
    eam_io_init = libutils::try_proc<eam_io_init_t>(EAMIO_DLL, "eam_io_init");
    eam_io_fini = libutils::try_proc<eam_io_fini_t>(EAMIO_DLL, "eam_io_fini");
    eam_io_get_keypad_state = libutils::try_proc<eam_io_get_keypad_state_t>(EAMIO_DLL, "eam_io_get_keypad_state");
    eam_io_get_sensor_state = libutils::try_proc<eam_io_get_sensor_state_t>(EAMIO_DLL, "eam_io_get_sensor_state");
    eam_io_read_card = libutils::try_proc<eam_io_read_card_t>(EAMIO_DLL, "eam_io_read_card");
    eam_io_card_slot_cmd = libutils::try_proc<eam_io_card_slot_cmd_t>(EAMIO_DLL, "eam_io_card_slot_cmd");
    eam_io_poll = libutils::try_proc<eam_io_poll_t>(EAMIO_DLL, "eam_io_poll");
    eam_io_get_config_api = libutils::try_proc<eam_io_get_config_api_t>(EAMIO_DLL, "eam_io_get_config_api");

    // initialize
    eam_io_set_loggers(&bt5api_log, &bt5api_log, &bt5api_log, &bt5api_log);
    eam_io_init(&bt5api_thread_create, &bt5api_thread_join, &bt5api_thread_destroy);

    // bt5api workaround for games with 2 card readers (NFCeAmuse cares about this)
    if (eamuse_get_game_keypads() > 1) {
        bt5api_poll_reader_card(1);
        bt5api_poll_reader_card(0);
    }

    // done
    log_info("bt5api", "done initializing");
}

void bt5api_hook(HINSTANCE module) {
    libraryhook_enable(module);

    // toastertools
    libraryhook_hook_proc("iidx_io_ext_get_16seg", games::iidx::get_16seg);
}

void bt5api_poll_reader_card(uint8_t unit_no) {

    // check if initialized or out of bounds
    if (!EAMIO_DLL || unit_no >= eamuse_get_game_keypads()) {
        return;
    }

    // poll
    if (!eam_io_poll(unit_no)) {
        log_warning("bt5api", "polling bt5api reader card {} returned failure",
                static_cast<int>(unit_no));
        return;
    }

    // get sensor state
    uint8_t sensor_state = eam_io_get_sensor_state(unit_no);
    
    // if we have a change in state.
    if(sensor_state !=  BT5API_CARD_STATES[unit_no]) {

        // card inserted into the back.
        if(sensor_state & (1 << EAM_IO_SENSOR_BACK)) {
            uint8_t card_id[8];
            
            log_info("bt5api", "card unit {} inserted", static_cast<int>(unit_no));
            
            // do the bt5 dance
            eam_io_card_slot_cmd(unit_no, EAM_IO_CARD_SLOT_CMD_CLOSE);
            eam_io_poll(unit_no);
            eam_io_card_slot_cmd(unit_no, EAM_IO_CARD_SLOT_CMD_READ);
            eam_io_poll(unit_no);
            
            // ask the api for the card, then insert it into our engine.
            eam_io_read_card(unit_no, card_id, 8);
            eamuse_card_insert(unit_no, card_id);
        }
        
        // if card removed entirely, do another bt5 dance!
        if(sensor_state == 0) {
            eam_io_card_slot_cmd(unit_no, EAM_IO_CARD_SLOT_CMD_CLOSE);
            eam_io_poll(unit_no);
            eam_io_card_slot_cmd(unit_no, EAM_IO_CARD_SLOT_CMD_OPEN);
            
            log_info("bt5api", "card unit {} removed", static_cast<int>(unit_no));
        }
    }
    
    // save prev state
    BT5API_CARD_STATES[unit_no] = sensor_state;
}
    
void bt5api_poll_reader_keypad(uint8_t unit_no) {

    // check if initialized or out of bounds
    if (!EAMIO_DLL || unit_no >= eamuse_get_game_keypads()) {
        return;
    }
    
    uint16_t keypad_current = eam_io_get_keypad_state(unit_no);
    uint16_t keypad_changes = ~BT5API_KEYPAD_STATES[unit_no] & keypad_current;
    
    // if user has pressed decimal for the first time, eject the slotted reader.
    if(keypad_changes & (1 << EAM_IO_KEYPAD_DECIMAL)) {
        log_info("bt5api", "card unit {} ejecting", static_cast<int>(unit_no));
        
        eam_io_card_slot_cmd(unit_no, EAM_IO_CARD_SLOT_CMD_EJECT);
    }
    
    // save the last pressed.
    BT5API_KEYPAD_STATES[unit_no] = keypad_current;

    // send off the state to the main engine.
    eamuse_set_keypad_overrides_bt5(unit_no, keypad_current);
}

void bt5api_dispose() {

    // check if initialized
    if (!EAMIO_DLL) {
        return;
    }

    // finish
    eam_io_fini();
}
