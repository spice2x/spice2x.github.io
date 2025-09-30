#include <set>
#include "logging.h"
#include "libutils.h"
#include "scope_guard.h"
#include "dependencies.h"

using loader_hint = std::tuple<std::string, std::string, std::string>;

namespace {
    // list of commonly missing dependencies and tips on where to get them
    std::vector<loader_hint> hints = {
        {
            "msvcr100.dll",
            "Visual Studio 2010 (VC++ 10.0) SP1 Redistributable",
            "Download and install from https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist",
        },
        {
            "d3dx9_43.dll",
            "DirectX End-User Runtimes",
            "Download and install from https://www.microsoft.com/en-gb/download/details.aspx?id=8109"
        },
        {
            "nvEncodeAPI64.dll",
            "NVIDIA Graphics Driver",
            "For non-NVIDIA GPUs, copy the stub file from the spice2x release .zip.",
        },
        {
            "nvcuda.dll",
            "NVIDIA Graphics Driver",
            "For non-NVIDIA GPUs, copy the stub file from the spice2x release .zip.",
        },
        {
            "nvcuvid.dll",
            "NVIDIA Graphics Driver",
            "For non-NVIDIA GPUs, copy the stub file from the spice2x release .zip.",
        },
    };

    std::set<std::wstring> failed = {};

    auto read_imports(const std::filesystem::path& path) -> std::vector<std::filesystem::path> {
        auto result = std::vector<std::filesystem::path> {};
        auto const file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return result;

        auto const file_ = scope_guard { [file] { CloseHandle(file); } };
        auto const mapping = CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

        if (!mapping)
            return result;

        auto const mapping_ = scope_guard { [mapping] { CloseHandle(mapping); } };
        auto const view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);

        if (!view)
            return result;

        auto const view_ = scope_guard { [view] { UnmapViewOfFile(view); } };
        auto const dos = static_cast<PIMAGE_DOS_HEADER>(view);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return result;

        auto const base = static_cast<std::uint8_t*>(view);
        auto const nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);

        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return result;

        auto const imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

        if (imports.VirtualAddress == 0)
            return result;

        auto desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + libutils::rva2offset(nt, imports.VirtualAddress));

        while (desc->Name != 0) {
            auto const filename = reinterpret_cast<char*>(base + libutils::rva2offset(nt, desc->Name));
            auto entry_path = path.parent_path().append(filename);

            // try to use library in the same directory if one exists
            // otherwise, assume system library and use filename only
            if (!std::filesystem::exists(entry_path))
                entry_path = filename;

            result.emplace_back(entry_path);
            desc++;
        }

        return result;
    }
}

namespace dependencies {
    auto walk(const std::filesystem::path& path, const std::string& prefix) -> bool {
        // try to load the library -- skip walking if it loads successfully
        if (auto const module = LoadLibraryW(path.c_str())) {
            FreeLibrary(module);
            return true;
        }

        auto const error = GetLastError();
        auto const filename = path.filename().string();

        log_misc("dependencies", "{}{}", prefix + (prefix.empty() ? "": "|-- "), filename);

        if (failed.contains(path))
            return false;

        failed.insert(path);

        auto const dependencies = read_imports(path);
        auto const next_prefix = prefix + (prefix.empty() ? "  ": "| ");

        if (!dependencies.empty()) {
            for (auto const& item: dependencies)
                walk(item, next_prefix);

            return false;
        }

        for (auto const& [dll, name, hint]: hints) {
            if (_stricmp(dll.c_str(), filename.c_str()) != 0)
                continue;

            log_warning("dependencies", "{}|-- [!] {}", next_prefix, name.c_str());
            log_warning("dependencies", "{}|--     {}", next_prefix, hint.c_str());
            return false;
        }

        log_warning("dependencies", "{}|-- [!] The library could not be loaded. ({})", next_prefix, error);
        return false;
    }
}