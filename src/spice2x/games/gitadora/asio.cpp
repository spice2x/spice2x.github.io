#include "asio.h"

#include <windows.h>
#include <cstring>

#include "avs/game.h"
#include "gitadora.h"
#include "util/detour.h"
#include "util/logging.h"

namespace games::gitadora {

    // Redirects the game's hard-coded "XONAR" ASIO driver lookup to the
    // driver name in ASIO_DRIVER by intercepting registry calls to
    // HKLM\SOFTWARE\ASIO. Sentinel HKEY values mark the redirected handles
    // so we can recognise them on subsequent reg* calls.

    static const HKEY PARENT_ASIO_REG_HANDLE = reinterpret_cast<HKEY>(0x4001);
    static const HKEY DEVICE_ASIO_REG_HANDLE = reinterpret_cast<HKEY>(0x4002);
    static const char *FAKE_ASIO_DEVICE_NAME = "XONAR";

    static decltype(RegCloseKey) *RegCloseKey_orig = nullptr;
    static decltype(RegEnumKeyA) *RegEnumKeyA_orig = nullptr;
    static decltype(RegOpenKeyExA) *RegOpenKeyExA_orig = nullptr;
    static decltype(RegQueryValueExA) *RegQueryValueExA_orig = nullptr;

    static HKEY real_asio_reg_handle = nullptr;
    static HKEY real_asio_device_reg_handle = nullptr;

    static LONG WINAPI RegOpenKeyExA_hook(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired,
            PHKEY phkResult)
    {
        // ASIO\XONAR redirect to ASIO\<configured>
        if (ASIO_DRIVER.has_value() &&
            lpSubKey != nullptr &&
            phkResult != nullptr &&
            hKey == PARENT_ASIO_REG_HANDLE &&
            _stricmp(lpSubKey, FAKE_ASIO_DEVICE_NAME) == 0) {

            *phkResult = DEVICE_ASIO_REG_HANDLE;

            log_info("gitadora::asio", "replacing '{}' with '{}'", lpSubKey, ASIO_DRIVER.value());
            const auto result = RegOpenKeyExA_orig(
                    real_asio_reg_handle,
                    ASIO_DRIVER.value().c_str(),
                    ulOptions,
                    samDesired,
                    &real_asio_device_reg_handle);

            if (result != ERROR_SUCCESS) {
                log_warning(
                    "gitadora::asio",
                    "failed to open registry subkey '{}', error=0x{:x}",
                    ASIO_DRIVER.value(), result);
                log_warning(
                    "gitadora::asio",
                    "due to improper ASIO setting, audio init will fail");
            }

            return result;
        }

        // open of the ASIO root: hand back a sentinel
        if (ASIO_DRIVER.has_value() &&
            lpSubKey != nullptr &&
            phkResult != nullptr &&
            hKey == HKEY_LOCAL_MACHINE &&
            _stricmp(lpSubKey, "software\\asio") == 0)
        {
            *phkResult = PARENT_ASIO_REG_HANDLE;
            return RegOpenKeyExA_orig(hKey, lpSubKey, ulOptions, samDesired, &real_asio_reg_handle);
        }

        return RegOpenKeyExA_orig(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    }

    static LONG WINAPI RegEnumKeyA_hook(HKEY hKey, DWORD dwIndex, LPSTR lpName, DWORD cchName) {
        if (hKey == PARENT_ASIO_REG_HANDLE && ASIO_DRIVER.has_value()) {
            if (dwIndex == 0) {
                // forward to real handle just to verify the key exists; we
                // overwrite the name with our fake driver string regardless
                auto ret = RegEnumKeyA_orig(real_asio_reg_handle, dwIndex, lpName, cchName);
                if (ret == ERROR_SUCCESS && lpName != nullptr) {
                    log_info("gitadora::asio", "stubbing '{}' with '{}'", lpName, FAKE_ASIO_DEVICE_NAME);
                    strncpy(lpName, FAKE_ASIO_DEVICE_NAME, cchName);
                }
                return ret;
            } else {
                return ERROR_NO_MORE_ITEMS;
            }
        }

        return RegEnumKeyA_orig(hKey, dwIndex, lpName, cchName);
    }

    static LONG WINAPI RegQueryValueExA_hook(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
            LPBYTE lpData, LPDWORD lpcbData)
    {
        if (ASIO_DRIVER.has_value() &&
            lpValueName != nullptr &&
            lpData != nullptr &&
            lpcbData != nullptr &&
            hKey == DEVICE_ASIO_REG_HANDLE) {
            if (_stricmp(lpValueName, "Description") == 0) {
                // engine may verify the driver name after open; ensure it still
                // sees something containing "XONAR" so the substring check passes
                const size_t len = strlen(FAKE_ASIO_DEVICE_NAME) + 1;
                if (*lpcbData < len) {
                    *lpcbData = static_cast<DWORD>(len);
                    return ERROR_MORE_DATA;
                }
                memcpy(lpData, FAKE_ASIO_DEVICE_NAME, len);
                *lpcbData = static_cast<DWORD>(len);
                if (lpType != nullptr) {
                    *lpType = REG_SZ;
                }
                return ERROR_SUCCESS;
            }

            // for everything else (CLSID etc.) defer to the real driver subkey
            hKey = real_asio_device_reg_handle;
        }

        return RegQueryValueExA_orig(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    }

    static LONG WINAPI RegCloseKey_hook(HKEY hKey) {
        if (hKey == PARENT_ASIO_REG_HANDLE) {
            if (real_asio_reg_handle != nullptr) {
                RegCloseKey_orig(real_asio_reg_handle);
                real_asio_reg_handle = nullptr;
            }
            return ERROR_SUCCESS;
        }

        if (hKey == DEVICE_ASIO_REG_HANDLE) {
            if (real_asio_device_reg_handle != nullptr) {
                RegCloseKey_orig(real_asio_device_reg_handle);
                real_asio_device_reg_handle = nullptr;
            }
            return ERROR_SUCCESS;
        }

        return RegCloseKey_orig(hKey);
    }

    void asio_hook_init() {
        if (!ASIO_DRIVER.has_value()) {
            return;
        }

        log_info("gitadora::asio", "installing ASIO driver redirect: XONAR -> {}", ASIO_DRIVER.value());

        RegCloseKey_orig = detour::iat_try(
                "RegCloseKey", RegCloseKey_hook, avs::game::DLL_INSTANCE);
        RegEnumKeyA_orig = detour::iat_try(
                "RegEnumKeyA", RegEnumKeyA_hook, avs::game::DLL_INSTANCE);
        RegOpenKeyExA_orig = detour::iat_try(
                "RegOpenKeyExA", RegOpenKeyExA_hook, avs::game::DLL_INSTANCE);
        RegQueryValueExA_orig = detour::iat_try(
                "RegQueryValueExA", RegQueryValueExA_hook, avs::game::DLL_INSTANCE);
    }
}
