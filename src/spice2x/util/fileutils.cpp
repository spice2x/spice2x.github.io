#include "fileutils.h"

#include <fstream>

#include <sys/stat.h>
#include <direct.h>

#include "logging.h"

bool fileutils::file_exists(LPCSTR szPath) {
    DWORD dwAttrib = GetFileAttributesA(szPath);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
            !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}
bool fileutils::file_exists(LPCWSTR szPath) {
    DWORD dwAttrib = GetFileAttributesW(szPath);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
            !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool fileutils::file_exists(const std::string &file_path) {
    return file_exists(file_path.c_str());
}
bool fileutils::file_exists(const std::filesystem::path &file_path) {
    return file_exists(file_path.c_str());
}

bool fileutils::verify_header_pe(const std::filesystem::path &file_path) {
    if (!file_exists(file_path)) {
        return false;
    }

    // open file
    HANDLE dll_file;
    dll_file = CreateFileW(
            file_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0);
    if (!dll_file) {
        return false;
    }

    // get size
    LARGE_INTEGER dll_file_size;
    if (!GetFileSizeEx(dll_file, &dll_file_size) || (size_t) dll_file_size.QuadPart < sizeof(PIMAGE_DOS_HEADER)) {
        CloseHandle(dll_file);
        return false;
    }

    // create file mapping
    HANDLE dll_mapping = CreateFileMappingW(dll_file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!dll_mapping) {
        CloseHandle(dll_file);
        return false;
    }

    // map view of file
    LPVOID dll_file_base = MapViewOfFile(dll_mapping, FILE_MAP_READ, 0, 0, 0);
    if (!dll_file_base) {
        CloseHandle(dll_file);
        CloseHandle(dll_mapping);
        return false;
    }

    // verify header
    bool valid = false;
    auto dll_dos = reinterpret_cast<PIMAGE_DOS_HEADER>(dll_file_base);
    if (dll_dos->e_magic == IMAGE_DOS_SIGNATURE) {

        // verify architecture
        auto dll_nt = (PIMAGE_NT_HEADERS) ((uint8_t*) dll_dos + dll_dos->e_lfanew);
        if ((size_t) dll_nt - (size_t) dll_file_base < (size_t) dll_file_size.QuadPart) {
            auto dll_file_header = (PIMAGE_FILE_HEADER) &dll_nt->FileHeader;
            if ((size_t) dll_file_header - (size_t) dll_file_base < (size_t) dll_file_size.QuadPart) {
#if SPICE64
                valid = dll_file_header->Machine == IMAGE_FILE_MACHINE_AMD64;
                if (!valid) {
                    log_fatal("fileutils",
                            "{} (32 bit) can't be loaded using spice64.exe - please use spice.exe for this game.",
                            file_path.string());
                }
#else
                valid = dll_file_header->Machine == IMAGE_FILE_MACHINE_I386;
                if (!valid) {
                    log_fatal("fileutils",
                            "{} (64 bit) can't be loaded using spice.exe - please use spice64.exe for this game.",
                            file_path.string());
                }
#endif
            }
        }
    }

    // clean up and return
    UnmapViewOfFile(dll_file_base);
    CloseHandle(dll_file);
    CloseHandle(dll_mapping);
    return valid;
}

bool fileutils::version_pe(const std::filesystem::path &file_path, char *ver) {
    DWORD dwHandle = 0;
    DWORD dwLen = GetFileVersionInfoSizeW(file_path.c_str(), &dwHandle);
    if (!dwLen) {
        return false;
    }

    auto buf = std::make_unique<uint8_t[]>(dwLen);
    if (!GetFileVersionInfoW(file_path.c_str(), dwHandle, dwLen, buf.get())) {
        return false;
    }

    VS_FIXEDFILEINFO *pvi = nullptr;
    UINT uLen = 0;
    if (!VerQueryValueW(buf.get(), L"\\", reinterpret_cast<void **>(&pvi), &uLen)) {
        return false;
    }

    sprintf(ver, "%d.%d.%d.%d",
            (int) (pvi->dwProductVersionMS >> 16),
            (int) (pvi->dwFileVersionMS & 0xFFFF),
            (int) (pvi->dwFileVersionLS >> 16),
            (int) (pvi->dwFileVersionLS & 0xFFFF));

    return true;
}

bool fileutils::dir_exists(const std::filesystem::path &dir_path) {
    std::error_code err;

    auto status = std::filesystem::status(dir_path, err);

    if (err) {
        return false;
    }

    return std::filesystem::is_directory(status);
}

bool fileutils::dir_create(const std::filesystem::path &dir_path) {
    std::error_code err;

    auto ret = std::filesystem::create_directory(dir_path, err);

    return ret && !err;
}

bool fileutils::dir_create_log(const std::string_view &module, const std::filesystem::path &dir_path) {
    std::error_code err;

    auto ret = std::filesystem::create_directory(dir_path, err);

    if (err) {
        log_warning(module, "failed to create directory '{}': {}", dir_path.string(), err.message());
    } else if (ret) {
        log_misc(module, "created directory '{}'", dir_path.string());
    }

    return ret && !err;
}

bool fileutils::dir_create_recursive(const std::filesystem::path &dir_path) {
    std::error_code err;

    auto ret = std::filesystem::create_directories(dir_path, err);

    return ret && !err;
}

bool fileutils::dir_create_recursive_log(const std::string_view &module, const std::filesystem::path &dir_path) {
    std::error_code err;

    auto ret = std::filesystem::create_directories(dir_path, err);

    if (err) {
        log_warning(module, "failed to create directory (recursive) '{}': {}", dir_path.string(), err.message());
    } else if (ret) {
        log_misc(module, "created directory (recursive) '{}'", dir_path.string());
    }

    return ret && !err;
}

void fileutils::dir_scan(const std::string &path, std::vector<std::string> &vec, bool recursive) {

    // check directory
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        if (recursive) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
                if (!std::filesystem::is_directory(entry)) {
                    auto path = entry.path().string();
                    vec.emplace_back(std::move(path));
                }
            }
        } else {
            for (const auto &entry : std::filesystem::directory_iterator(path)) {
                if (!std::filesystem::is_directory(entry)) {
                    auto path = entry.path().string();
                    vec.emplace_back(std::move(path));
                }
            }
        }
    }

    // determinism
    std::sort(vec.begin(), vec.end());
}

bool fileutils::text_write(const std::filesystem::path &file_path, std::string text) {
    std::ofstream out(file_path, std::ios::out | std::ios::binary);
    if (out) {
        out << text;
        out.close();
        return true;
    }
    return false;
}

std::string fileutils::text_read(const std::filesystem::path &file_path) {
    std::ifstream in(file_path, std::ios::in | std::ios::binary);
    if (in) {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.reserve(in.tellg());
        in.seekg(0, std::ios::beg);
        std::copy(std::istreambuf_iterator<char>(in),
                  std::istreambuf_iterator<char>(),
                  std::back_inserter(contents));
        in.close();
        return contents;
    }
    return std::string();
}


bool fileutils::bin_write(const std::filesystem::path &path, uint8_t *data, size_t len) {

    // write to disk
    std::ofstream out(path, std::ios::out | std::ios::binary);
    if (out) {
        out.write((const char*) data, len);
        out.close();
        return true;
    }
    return false;
}

std::vector<uint8_t> *fileutils::bin_read(const std::filesystem::path &path) {

    // read from disk
    std::ifstream in(path, std::ios::in | std::ios::binary | std::ios::ate);
    auto contents = new std::vector<uint8_t>();
    if (in) {
        contents->resize((unsigned) in.tellg());
        in.seekg(0, std::ios::beg);
        if (!in.read((char*) contents->data(), contents->size())) {
            contents->clear();
        }
        in.close();
    }
    return contents;
}

std::filesystem::path fileutils::get_config_file_path(const std::string module, const std::string filename, bool* file_exists) {
    // try %appdata%\spice2x path first, if it exists
    const auto appdata_spice2x = std::filesystem::path(_wgetenv(L"APPDATA")) / "spice2x" / filename;
    if (fileutils::file_exists(appdata_spice2x)) {
        log_info(module, "loading config from %appdata%\\spice2x\\{}", filename);
        if (file_exists) {
            *file_exists = true;
        }
        return appdata_spice2x;
    }

    // fallback to older %appdata% path (older spice2x or mainline spicetools), if it exists
    const auto appdata = std::filesystem::path(_wgetenv(L"APPDATA")) / filename;
    if (fileutils::file_exists(appdata)) {
        log_info(module, "loading config from %appdata%\\{}", filename);
        if (file_exists) {
            *file_exists = true;
        }
        return appdata;
    }

    // prefer new path if no existing file found
    if (file_exists) {
        *file_exists = false;
    }
    return appdata_spice2x;
}

bool fileutils::write_config_file(const std::string_view &module, const std::filesystem::path path, std::string text) {
    if (!std::filesystem::exists(path.parent_path()) &&
        !fileutils::dir_create_recursive(path.parent_path())) {
        return false;
    }

    const auto appdata = std::filesystem::path(_wgetenv(L"APPDATA")).string();
    auto censored = path.string();
    censored.replace(censored.find(appdata),appdata.length(),"%appdata%");
    log_info(module, "saving config file: {}", censored);
    return fileutils::text_write(path, text);
}