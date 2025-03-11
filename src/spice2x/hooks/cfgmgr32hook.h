#pragma once

#include <string>

#include <windows.h>

struct CFGMGR32_HOOK_SETTING {
    DWORD device_instance;
    DWORD parent_instance;
    std::string device_node_id;
    std::string device_id;
};

void cfgmgr32hook_init(HINSTANCE module);
void cfgmgr32hook_add(CFGMGR32_HOOK_SETTING setting);
