#pragma once

#include <atomic>
#include <vector>

#include <windows.h>

#include "module.h"

namespace acio {

    // globals
    extern HINSTANCE DLL_INSTANCE;
    extern std::vector<acio::ACIOModule *> MODULES;
    extern std::atomic<bool> IO_INIT_IN_PROGRESS;

    void attach();
    void attach_icca();
    void detach();
}
