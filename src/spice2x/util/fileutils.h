#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

namespace fileutils {

    // file existence
    bool file_exists(LPCSTR szPath);
    bool file_exists(LPCWSTR szPath);
    bool file_exists(const std::string &file_path);
    bool file_exists(const std::filesystem::path &file_path);

    // file headers
    bool verify_header_pe(const std::filesystem::path &file_path);

    // versions
    bool version_pe(const std::filesystem::path &file_path, char *ver);

    // directories
    bool dir_exists(const std::filesystem::path &dir_path);
    bool dir_create(const std::filesystem::path &dir_path);
    bool dir_create_log(const std::string_view &module, const std::filesystem::path &dir_path);
    bool dir_create_recursive(const std::filesystem::path &dir_path);
    bool dir_create_recursive_log(const std::string_view &module, const std::filesystem::path &dir_path);
    void dir_scan(const std::string &path, std::vector<std::string> &vec, bool recursive);

    // IO
    bool text_write(const std::filesystem::path &file_path, std::string text);
    std::string text_read(const std::filesystem::path &file_path);
    bool bin_write(const std::filesystem::path &path, uint8_t *data, size_t len);
    std::vector<uint8_t> *bin_read(const std::filesystem::path &path);
}
