#pragma once

#include <string>
#include <vector>
#include <windows.h>

#include "sdk/include/spicesdk.h"

namespace sdk {

struct SdkModule {
    std::string dll;
    HINSTANCE module;
};

namespace modules {

SPICE_SDK_STATUS_CODE get_module_info(const wchar_t *module_name, SPICE_SDK_MODULE_INFO *info);
SPICE_SDK_STATUS_CODE get_plugin_directory(const std::vector<SdkModule> &registered_modules,
    const void *plugin_address, wchar_t *buffer, uint32_t *size);

}

}