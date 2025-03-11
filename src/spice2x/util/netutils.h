#pragma once

#include <vector>
#include <string>

namespace netutils {
    std::vector<std::string> get_local_addresses();
    std::string http_status_reason_phrase(int code);
}
