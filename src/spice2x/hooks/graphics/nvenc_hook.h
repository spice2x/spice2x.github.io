#pragma once

#ifdef SPICE64

namespace nvenc_hook {

    extern bool FORCE_DISABLE;
    extern std::optional<std::string> VIDEO_CQP_STRING_OVERRIDE;
    void initialize();
}

#endif
