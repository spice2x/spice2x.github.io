#pragma once

#include "sdk/include/spicesdk.h"

namespace sample_imgui {

SPICE_SDK_STATUS_CODE initialize(const SPICE_SDK_V0 &spice);
void toggle();

}