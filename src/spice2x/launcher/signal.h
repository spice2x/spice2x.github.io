#pragma once

#include <string>
#include <cstdint>

namespace launcher::signal {

    // settings
    extern bool DISABLE;
    extern bool USE_VEH_WORKAROUND;

    // error cases
    extern bool SUPERSTEP_SOUND_ERROR;
    extern bool AVS_DIR_CREATION_FAILURE;
    extern std::string AVS_XML_PATH;
    extern std::string AVS_SRC_PATH;
    extern bool D3D9_CREATE_DEVICE_FAILED;
    extern uint32_t D3D9_CREATE_DEVICE_FAILED_HRESULT;

    //void print_stacktrace();
    void attach();
    void init();
}
