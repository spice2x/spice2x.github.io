#include <winsock2.h>
#include <ws2tcpip.h>
#include <random>

#include "utils.h"

#if !SPICE_XP
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
// older numbering used by Win10 1809..1909
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#endif

const char *inet_ntop(short af, const void *src, char *dst, DWORD size) {

    // prepare storage
    struct sockaddr_storage ss {};
    ZeroMemory(&ss, sizeof(ss));
    ss.ss_family = af;

    // IPv6 compatibility
    switch (af) {
        case AF_INET:
            ((struct sockaddr_in *) &ss)->sin_addr = *(struct in_addr *) src;
            break;
        case AF_INET6:
            ((struct sockaddr_in6 *) &ss)->sin6_addr = *(struct in6_addr *) src;
            break;
        default:
            return nullptr;
    }

    // convert to string
    int result = WSAAddressToStringA((struct sockaddr *) &ss, sizeof(ss), nullptr, dst, &size);

    // return on success
    return (result == 0) ? dst : nullptr;
}

BOOL CALLBACK _find_window_begins_with_cb(HWND wnd, LPARAM lParam) {
    auto windows = reinterpret_cast<std::vector<HWND> *>(lParam);
    windows->push_back(wnd);
    return TRUE;
}

std::wstring s2ws(const std::string &str) {
    if (str.empty()) {
        return std::wstring();
    }

    int length = MultiByteToWideChar(CP_ACP, 0, str.data(), -1, nullptr, 0);
    if (length == 0) {
        log_fatal("utils", "failed to get length of wide string: {}", get_last_error_string());
    }

    std::wstring buffer;
    buffer.resize(length - 1);

    length = MultiByteToWideChar(CP_ACP, 0, str.data(), -1, buffer.data(), length);
    if (length == 0) {
        log_fatal("utils", "failed to convert string to wide string: {}", get_last_error_string());
    }

    return buffer;
}

std::string ws2s(const std::wstring &wstr) {
    if (wstr.empty()) {
        return std::string();
    }

    int length = WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        log_fatal("utils", "failed to get length of wide string: {}", get_last_error_string());
    }

    std::string buffer;
    buffer.resize(length - 1);

    length = WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1, buffer.data(), length, nullptr, nullptr);
    if (length == 0) {
        log_fatal("utils", "failed to convert string to wide string: {}", get_last_error_string());
    }

    return buffer;
}

bool acquire_shutdown_privs() {

    // check if already acquired
    static bool acquired = false;
    if (acquired)
        return true;

    // get process token
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    // get the LUID for the shutdown privilege
    TOKEN_PRIVILEGES tkp;
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // get the shutdown privilege for this process
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES) NULL, 0);

    // check for error
    bool success = GetLastError() == ERROR_SUCCESS;
    if (success)
        acquired = true;
    return success;
}

void generate_ea_card(char card[17]) {
    // don't ask why the existing codebase uses 18 char array when there are only 16+1 chars

    // create random
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<> uniform(0, 15);

    // randomize card
    strcpy(card, "E0040100");
    char hex[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
    for (int i = 8; i < 16; i++) {
        card[i] = hex[uniform(generator)];
    }

    // terminate and flush
    card[16] = 0;
}

void set_window_dark_titlebar(HWND hWnd, bool force_dark) {
#if !SPICE_XP
    if (hWnd == nullptr) {
        return;
    }

    // AppsUseLightTheme == 0 means the dark app theme is selected
    DWORD light = 1;
    if (!force_dark) {
        DWORD size = sizeof(light);
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
            RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&light), &size);
            RegCloseKey(key);
        }
    }

    BOOL dark = (force_dark || light == 0) ? TRUE : FALSE;

    // fall back to the older attribute numbering on Win10 1809..1909
    if (FAILED(DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD,
            &dark, sizeof(dark));
    }
#else
    (void) hWnd;
    (void) force_dark;
#endif
}
