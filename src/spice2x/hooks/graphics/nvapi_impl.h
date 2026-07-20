#pragma once

#ifdef SPICE64

#include <cstdint>

namespace nvapi_impl {

bool initialize(uint32_t main_refresh_hz, uint32_t sub_refresh_hz);

}

#endif