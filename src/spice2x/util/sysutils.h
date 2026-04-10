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
        bool is_primary;
        uint32_t adapter_id_LowPart;
        int32_t adapter_id_HighPart;
        uint32_t id;
    };

    const std::vector<MonitorEntry> &enumerate_monitors();

    extern std::string SECOND_MONITOR_OVERRIDE;
    void hook_EnumDisplayDevicesA();    
}