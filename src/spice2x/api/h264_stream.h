#pragma once

#include <memory>

#include "stream_format.h"

namespace api {

    // null when the encoder cannot be started; only defined when SPICE_H264 is set
    std::unique_ptr<StreamWriter> make_h264_writer(int quality, int fps);
}
