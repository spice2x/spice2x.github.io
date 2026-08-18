#pragma once

#include <memory>

#include "stream_format.h"

namespace api {

    // null when the encoder cannot be started; only defined when SPICE_H264 is set
    std::unique_ptr<StreamWriter> make_h264_writer(int quality, int fps);

    // the same encoder with no container, for clients feeding a hardware decoder directly
    std::unique_ptr<StreamWriter> make_annexb_writer(int quality, int fps);
}
