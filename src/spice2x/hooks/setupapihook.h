#pragma once

#include <windows.h>

struct SETUPAPI_SETTINGS {
    unsigned int class_guid[4] {};
    char property_devicedesc[256] {};
    char property_hardwareid[256] {};
    DWORD property_address[2] {};
    char interface_detail[256] {};
};

void setupapihook_init(HINSTANCE module);
void setupapihook_add(SETUPAPI_SETTINGS settings);
