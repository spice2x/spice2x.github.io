#include "asio_driver_scan.h"

#include <algorithm>

#include <windows.h>

#include "util/utils.h"

namespace hooks::audio {

    static constexpr char ASIO_REG_PATH[] = "software\\asio";
    static constexpr char ASIO_REG_DESC[] = "description";

    // enumerate a single registry view, appending to entries while merging
    // duplicates discovered in another view. Drivers are matched by name (not
    // CLSID): the game's ASIO loader selects drivers by name, and some vendors
    // register the same CLSID under different 32-bit/64-bit names (e.g. "XONAR
    // SOUND CARD" vs "XONAR SOUND CARD(64)"), which are distinct user choices.
    static void scan_view(
            REGSAM wow64_flag,
            bool is_64bit,
            std::vector<AsioDriverScanEntry> &entries) {

        HKEY hkEnum = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ASIO_REG_PATH, 0,
                KEY_READ | wow64_flag, &hkEnum) != ERROR_SUCCESS) {
            return;
        }

        char key_name[256];
        for (DWORD index = 0;
             RegEnumKeyA(hkEnum, index, key_name, sizeof(key_name)) == ERROR_SUCCESS;
             index++) {

            // read description (display name), fall back to the key name.
            // RegOpenKeyExA + RegQueryValueExA is used instead of RegGetValueA
            // because the latter is unavailable on Windows XP.
            char desc[256] = { 0 };
            DWORD size = sizeof(desc);
            std::string name = key_name;
            HKEY hkDriver = nullptr;
            if (RegOpenKeyExA(hkEnum, key_name, 0,
                    KEY_QUERY_VALUE | wow64_flag, &hkDriver) == ERROR_SUCCESS) {
                DWORD type = 0;
                if (RegQueryValueExA(hkDriver,
                                     ASIO_REG_DESC,
                                     nullptr,
                                     &type,
                                     reinterpret_cast<LPBYTE>(desc),
                                     &size) == ERROR_SUCCESS
                        && type == REG_SZ && desc[0]) {
                    // ensure null termination
                    desc[sizeof(desc) - 1] = '\0';
                    name = desc;
                }
                RegCloseKey(hkDriver);
            }

            // merge with an existing entry from the other view (match by name)
            const std::string name_lower = strtolower(name);
            auto it = std::find_if(entries.begin(), entries.end(), [&](const auto &e) {
                return strtolower(e.name) == name_lower;
            });
            if (it == entries.end()) {
                entries.push_back({ name });
                it = entries.end() - 1;
            }
            it->found_32bit |= !is_64bit;
            it->found_64bit |= is_64bit;
        }

        RegCloseKey(hkEnum);
    }

    std::vector<AsioDriverScanEntry> scan_asio_drivers() {
        std::vector<AsioDriverScanEntry> entries;

        // 64-bit view first so it wins ordering when present in both
        scan_view(KEY_WOW64_64KEY, true, entries);
        scan_view(KEY_WOW64_32KEY, false, entries);

        return entries;
    }
}
