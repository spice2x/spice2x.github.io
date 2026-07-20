#pragma once

#ifdef SPICE64

#include <cstdint>
#include <windows.h>

namespace nvapi_impl {

bool initialize(HINSTANCE dll, uint32_t main_refresh_hz, uint32_t sub_refresh_hz);

}

#endif
