#include "logger.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "avs/ea3.h"
#include "launcher/launcher.h"
#include "util/utils.h"

#define FOREGROUND_GREY   (8)
#define FOREGROUND_WHITE  (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN)
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)

namespace logger {

    // settings
    bool BLOCKING = false;
    bool COLOR = true;

    // state
    static bool RUNNING = false;
    static WORD DEFAULT_ATTRIBUTES = 0;
    static std::mutex EVENT_MUTEX;
    static std::condition_variable EVENT_CV;
    static std::thread *THREAD = nullptr;
    static std::mutex OUTPUT_MUTEX;
    static bool OUTPUT_BUFFER_HOT = false;
    static std::vector<std::pair<std::string, Style>> OUTPUT_BUFFER1;
    static std::vector<std::pair<std::string, Style>> OUTPUT_BUFFER2;
    static std::vector<std::pair<std::string, Style>> *OUTPUT_BUFFER = &OUTPUT_BUFFER1;
    static std::vector<std::pair<std::string, Style>> *OUTPUT_BUFFER_SWAP = &OUTPUT_BUFFER2;
    static std::vector<std::pair<LogHook_t, void*>> HOOKS;

    static inline std::vector<std::pair<std::string, Style>> *output_buffer_swap() {
        OUTPUT_MUTEX.lock();
        auto buffer = OUTPUT_BUFFER;
        std::swap(OUTPUT_BUFFER, OUTPUT_BUFFER_SWAP);
        OUTPUT_MUTEX.unlock();

        return buffer;
    }

    static void save_default_console_attributes(HANDLE hTerminal) {
        CONSOLE_SCREEN_BUFFER_INFO info;

        if (GetConsoleScreenBufferInfo(hTerminal, &info)) {
            DEFAULT_ATTRIBUTES = info.wAttributes;
        }
    }

    static void set_console_color(HANDLE hTerminal, WORD foreground) {
        CONSOLE_SCREEN_BUFFER_INFO info;

        if (!GetConsoleScreenBufferInfo(hTerminal, &info)) {
            return;
        }

        info.wAttributes &= ~(info.wAttributes & 0x0F);
        if (foreground == FOREGROUND_YELLOW)
            info.wAttributes |= foreground;
        else
            info.wAttributes |= foreground | FOREGROUND_INTENSITY;

        SetConsoleTextAttribute(hTerminal, info.wAttributes);
    }

    static void output_buffer_flush() {

        // get buffer and swap
        auto buffer = output_buffer_swap();

        // return early if no messages to process
        if (buffer->empty()) {
            return;
        }

        // get terminal handle
        HANDLE hTerminal = GetStdHandle(STD_OUTPUT_HANDLE);

        if (logger::COLOR) {

            // save default terminal attributes
            if (!DEFAULT_ATTRIBUTES) {
                save_default_console_attributes(hTerminal);
            }

            // set initial style
            set_console_color(hTerminal, FOREGROUND_WHITE);
        }

        // write to console and file
        DWORD result;
        Style last_style = DEFAULT;
        for (auto &content : *buffer) {

            // set style if color mode enabled
            if (logger::COLOR && last_style != content.second) {
                last_style = content.second;

                switch (content.second) {
                    case Style::DEFAULT:
                        set_console_color(hTerminal, FOREGROUND_WHITE);
                        break;
                    case Style::GREY:
                        set_console_color(hTerminal, FOREGROUND_GREY);
                        break;
                    case Style::YELLOW:
                        set_console_color(hTerminal, FOREGROUND_YELLOW);
                        break;
                    case Style::RED:
                        set_console_color(hTerminal, FOREGROUND_RED);
                        break;
                }
            }

            // write to console
            WriteFile(hTerminal, content.first.c_str(), content.first.size(), &result, nullptr);

            // write to file
            if (LOG_FILE && LOG_FILE != INVALID_HANDLE_VALUE) {
                WriteFile(LOG_FILE, content.first.c_str(), content.first.size(), &result, nullptr);
            }
        }

        // clear buffer
        buffer->clear();

        // reset style
        if (logger::COLOR) {
            SetConsoleTextAttribute(hTerminal, DEFAULT_ATTRIBUTES);
        }
    }

    void start() {

        // don't start if blocking
        if (BLOCKING) {
            return;
        }

        // start logging thread
        RUNNING = true;
        THREAD = new std::thread([] {
            std::unique_lock<std::mutex> lock(EVENT_MUTEX);

            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

            // main loop
            while (RUNNING) {

                // wait for hot buffer
                EVENT_CV.wait(lock, [] { return OUTPUT_BUFFER_HOT; });
                OUTPUT_BUFFER_HOT = false;

                // flush buffer
                output_buffer_flush();
            }

            // make sure all is written
            output_buffer_flush();

            // flush writes to disk
            if (LOG_FILE && LOG_FILE != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(LOG_FILE);
            }

            // reset terminal
            if (logger::COLOR) {
                HANDLE hTerminal = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hTerminal, DEFAULT_ATTRIBUTES);
            }
        });
    }

    void stop() {
        log_info("logger", "stop");

        // clean up thread if required
        RUNNING = false;
        if (THREAD) {

            // fake notify to exit wait loop
            OUTPUT_BUFFER_HOT = true;
            EVENT_CV.notify_all();

            // join and clean up
            THREAD->join();
            delete THREAD;
            THREAD = nullptr;
        }
    }

    void push(std::string data, Style color, bool terminate) {

        // log hooks
        for (auto &hook : HOOKS) {
            std::string out;

            if (hook.first(hook.second, data, color, out)) {
                data = std::move(out);
                break;
            }
        }

        // check if empty
        if (data.empty()) {
            return;
        }

        // add to output
        OUTPUT_MUTEX.lock();
        OUTPUT_BUFFER->emplace_back(std::move(data), color);
        if (terminate) {
            OUTPUT_BUFFER->emplace_back("\r\n", color);
        }
        OUTPUT_MUTEX.unlock();

        // check if blocking or the logging thread is not running
        if (BLOCKING || !RUNNING) {

            // blocking guard
            static std::mutex blocking_lock;
            std::lock_guard<std::mutex> blocking_guard(blocking_lock);

            // immediately process logs
            output_buffer_flush();

        } else {

            // mark buffer as hot
            std::unique_lock<std::mutex> lock(EVENT_MUTEX);
            OUTPUT_BUFFER_HOT = true;
            EVENT_CV.notify_one();
        }
    }

    void hook_add(LogHook_t hook, void *user) {
        HOOKS.emplace_back(hook, user);
    }

    void hook_remove(LogHook_t hook, void *user) {
        HOOKS.erase(std::remove(HOOKS.begin(), HOOKS.end(), std::pair(hook, user)), HOOKS.end());
    }

    PCBIDFilter::PCBIDFilter() {
        hook_add(logger::PCBIDFilter::filter, this);
    }

    PCBIDFilter::~PCBIDFilter() {
        hook_remove(logger::PCBIDFilter::filter, this);
    }

    bool PCBIDFilter::filter(void *user, const std::string &data, Style style, std::string &out) {

        // check if PCBID in data
        if (data.find(avs::ea3::EA3_BOOT_PCBID) != std::string::npos) {

            // replace pcbid
            out = data;
            strreplace(out, avs::ea3::EA3_BOOT_PCBID, "[hidden]");
            return true;
        }

        // no replacement
        return false;
    }
}
