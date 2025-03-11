#pragma once

#include <windows.h>

namespace hooks::sleep {
    void init(DWORD ms_max, DWORD ms_replace, HMODULE module = nullptr);
}
