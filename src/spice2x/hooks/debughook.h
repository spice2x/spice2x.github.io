#pragma once

#include <windows.h>

#include "util/detour.h"
#include "util/logging.h"

namespace debughook {

    // settings
    extern bool DEBUGHOOK_LOGGING;

    // functions
    void attach();
    void detach();
}
