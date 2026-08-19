#pragma once

#include <memory>

#include "stream_format.h"

namespace api {

    // bare annex-b H.264; null when the build has no encoder
    std::unique_ptr<StreamWriter> make_h264_writer(int quality, int fps);
}
