#pragma once

#include <string>

namespace launcher::signal {

    // settings
    extern bool DISABLE;
    extern bool USE_VEH_WORKAROUND;
    extern bool SUPERSTEP_SOUND_ERROR;
    extern bool AVS_DIR_CREATION_FAILURE;
    extern std::string AVS_XML_PATH;
    extern std::string AVS_SRC_PATH;

    //void print_stacktrace();
    void attach();
    void init();
}
