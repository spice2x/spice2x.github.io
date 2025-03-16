#pragma once

#include <filesystem>

namespace clipboard {
    void copy_image(const std::filesystem::path path);
    void copy_text(const std::string& str);
    const std::string paste_text();
}
