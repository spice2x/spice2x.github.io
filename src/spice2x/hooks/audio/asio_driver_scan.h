#pragma once

#include <string>
#include <vector>

namespace hooks::audio {

    struct AsioDriverScanEntry {
        std::string name;
        bool found_32bit = false;
        bool found_64bit = false;
    };

    std::vector<AsioDriverScanEntry> scan_asio_drivers();
}
