#pragma once

#include <string>
#include <filesystem>

namespace dependencies {
    auto walk(const std::filesystem::path& path, const std::string& prefix = "") -> bool;
}