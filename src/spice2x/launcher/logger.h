#pragma once

#include <string>

namespace logger {

    // settings
    extern bool BLOCKING;
    extern bool COLOR;

    enum Style {
        DEFAULT = 0,
        GREY = 1,
        YELLOW = 2,
        RED = 3
    };

    void start();
    void stop();
    void push(std::string data, Style color, bool terminate = false);

    // log hooks
    typedef bool (*LogHook_t)(void *user, const std::string &data, Style style, std::string &out);
    void hook_add(LogHook_t hook, void *user);
    void hook_remove(LogHook_t hook, void *user);

    class PCBIDFilter {
    public:
        PCBIDFilter();
        ~PCBIDFilter();
    private:
        static bool filter(void *user, const std::string &data, Style style, std::string &out);
    };
}
