#pragma once

#include <vector>

#include <windows.h>

#include "module.h"

namespace acio {

    // globals
    extern HINSTANCE DLL_INSTANCE;
    extern std::vector<acio::ACIOModule *> MODULES;

    void attach();
    void attach_icca();
    void detach();
}
