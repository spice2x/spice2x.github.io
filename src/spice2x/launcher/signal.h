#pragma once

#include <string>
#include <cstdint>

namespace launcher::signal {

    // settings
    extern bool DISABLE;
    extern bool USE_VEH_WORKAROUND;

    void attach();
    void init();
}
