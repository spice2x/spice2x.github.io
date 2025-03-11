#pragma once

#include <string>

namespace richpresence {

    // settings
    namespace discord {
        extern std::string APPID_OVERRIDE;
    }

    void init();
    void update(const char *state);
    void shutdown();
}
