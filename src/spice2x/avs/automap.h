#pragma once

#include <string>

namespace avs::automap {

    extern bool ENABLED;
    extern bool DUMP;
    extern bool PATCH;
    extern bool JSON;
    extern bool RESTRICT_NETWORK;
    extern std::string DUMP_FILENAME;

    void enable();
    void disable();

    // log hooks
    typedef void (*AutomapHook_t)(void *user, const char *data);
    void hook_add(AutomapHook_t hook, void *user);
    void hook_remove(AutomapHook_t hook, void *user);
}
