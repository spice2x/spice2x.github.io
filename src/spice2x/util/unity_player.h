#pragma once

#include <string>

namespace unity_utils {
    std::string get_unity_player_args();
    void set_args(const std::string &args);
    void force_show_cursor(bool bShow);
}
