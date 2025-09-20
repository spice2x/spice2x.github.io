#pragma once

namespace launcher::signal {

    // settings
    extern bool DISABLE;
    extern bool USE_VEH_WORKAROUND;
    extern bool SUPERSTEP_SOUND_ERROR;

    //void print_stacktrace();
    void attach();
    void init();
}
