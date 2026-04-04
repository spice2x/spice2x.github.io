#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace sysutils {
    void print_smbios();
    void print_gpus();
    void print_os();

    struct MonitorEntry {
        std::string display_name;  // \\.\DISPLAY1
        std::wstring friendly_name; // Dell S2204T; wstring so that it can be converted to utf8 or string
    };

    const std::vector<MonitorEntry> &enumerate_monitors();
}