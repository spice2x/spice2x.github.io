#pragma once

#include <locale>
#include <string>
#include <vector>

#include <windows.h>

#include "logging.h"
#include "circular_buffer.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, lower, upper) (MIN(upper, MAX(x, lower)))

#define ARRAY_SETB(A, k) ((A)[((k) / 8)] |= (1 << ((k) % 8)))
#define ARRAY_CLRB(A, k) ((A)[((k) / 8)] &= ~(1 << ((k) % 8)))
#define ARRAY_TSTB(A, k) ((A)[((k) / 8)] & (1 << ((k) % 8)))

#ifndef RtlOffsetToPointer
#define RtlOffsetToPointer(B, O) ((PCHAR) (((PCHAR) (B)) + ((ULONG_PTR) (O))))
#endif

static const char HEX_LOOKUP_UPPERCASE[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

const char *inet_ntop(short af, const void *src, char *dst, DWORD size);

static inline bool string_begins_with(const std::string &s, const std::string &prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

static inline bool string_begins_with(const std::wstring &s, const std::wstring &prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

static inline bool string_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) {
        return false;
    }
    auto len1 = strlen(s);
    auto len2 = strlen(suffix);
    if (len2 > len1) {
        return false;
    }
    return strncmp(s + len1 - len2, suffix, len2) == 0;
}

static inline bool string_ends_with(const wchar_t *s, const wchar_t *suffix) {
    if (!s || !suffix) {
        return false;
    }
    auto len1 = wcslen(s);
    auto len2 = wcslen(suffix);
    if (len2 > len1) {
        return false;
    }
    return wcsncmp(s + len1 - len2, suffix, len2) == 0;
}

template<class Container>
static inline void strsplit(const std::string &str, Container &cont, char delim = ' ')
{
    std::istringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delim)) {
        cont.push_back(token);
    }
}

static inline void strreplace(std::string &s, const std::string &search, const std::string &replace) {
    size_t pos = 0;

    while ((pos = s.find(search, pos)) != std::string::npos) {
        s.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}

static inline std::string strtrim(const std::string& input) { 
    std::string output = input;
    // trim spaces
    output.erase(0, output.find_first_not_of("\t\n\v\f\r "));
    output.erase(output.find_last_not_of("\t\n\v\f\r ") + 1);
    return output;
} 

static inline std::string strtolower(const std::string& input) { 
    std::string output = strtrim(input);
    // replace with lower case
    std::transform(
        output.begin(), output.end(), output.begin(),
        [](unsigned char c){ return std::tolower(c); });
    return output;
} 

static inline int _hex2bin_helper(char input) {
    if (input >= '0' && input <= '9') {
        return input - '0';
    }
    if (input >= 'A' && input <= 'F') {
        return input - 'A' + 10;
    }
    if (input >= 'a' && input <= 'f') {
        return input - 'a' + 10;
    }
    return -1;
}

static inline bool hex2bin(const char *src, uint8_t *target) {
    while (*src) {
        if (!src[1]) {
            return false;
        }
        auto first = _hex2bin_helper(*src) * 16;
        auto second = _hex2bin_helper(src[1]);
        if (first < 0 || second < 0) {
            return false;
        }
        *(target++) = first + second;
        src += 2;
    }
    return true;
}

template<typename T>
static inline std::string bin2hex(T data) {
    std::string str;

    str.reserve(sizeof(T) * 2);

    for (size_t i = 0; i < sizeof(T); i++) {
        auto ch = ((uint8_t *) &data)[i];
        str.push_back(HEX_LOOKUP_UPPERCASE[(ch & 0xF0) >> 4]);
        str.push_back(HEX_LOOKUP_UPPERCASE[ch & 0x0F]);
    }
    return str;
}

template<typename T>
static inline std::string bin2hex(T *data, size_t size) {
    std::string str;

    str.reserve(size * 2);

    auto bytes = reinterpret_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++) {
        const auto ch = bytes[i];
        str.push_back(HEX_LOOKUP_UPPERCASE[(ch & 0xF0) >> 4]);
        str.push_back(HEX_LOOKUP_UPPERCASE[ch & 0x0F]);
    }

    return str;
}

template<typename T>
static inline std::string bin2hex(const std::vector<T> &data) {
    std::string str;

    str.reserve(data.size() * 2);

    for (const auto ch : data) {
        str.push_back(HEX_LOOKUP_UPPERCASE[(ch & 0xF0) >> 4]);
        str.push_back(HEX_LOOKUP_UPPERCASE[ch & 0x0F]);
    }

    return str;
}

template<typename T>
static inline std::string bin2hex(const circular_buffer<T> &data) {
    std::string str;

    str.reserve(data.size() * 2);

    for (size_t i = 0; i < data.size(); i++) {
        const auto ch = data.peek(i);
        str.push_back(HEX_LOOKUP_UPPERCASE[(ch & 0xF0) >> 4]);
        str.push_back(HEX_LOOKUP_UPPERCASE[ch & 0x0F]);
    }

    return str;
}

static inline bool file_exists(const std::string &name) {
    auto dwAttrib = GetFileAttributes(name.c_str());

    return dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
}

static inline std::string get_window_title(HWND hWnd) {
    char wnd_title[256] { 0 };
    GetWindowTextA(hWnd, wnd_title, sizeof(wnd_title));

    return std::string(wnd_title);
}

static inline std::string GetActiveWindowTitle() {
    return get_window_title(GetForegroundWindow());
}

BOOL CALLBACK _find_window_begins_with_cb(HWND wnd, LPARAM lParam);

static inline std::vector<HWND> find_windows_beginning_with(const std::string &title) {

    // get all windows
    DWORD dwThreadID = GetCurrentThreadId();
    HDESK hDesktop = GetThreadDesktop(dwThreadID);
    std::vector<HWND> windows;
    EnumDesktopWindows(hDesktop, _find_window_begins_with_cb, reinterpret_cast<LPARAM>(&windows));

    // check window titles
    windows.erase(
        std::remove_if(
            windows.begin(),
            windows.end(),
            [title](HWND hWnd) {
                return !string_begins_with(get_window_title(hWnd), title);
            }
        ),
        windows.end()
    );

    // return found windows
    return windows;
}

// exists for compat only; prefer to use FindProcessWindowBeginsWith instead
static inline HWND FindWindowBeginsWith(std::string title) {

    // get all windows
    DWORD dwThreadID = GetCurrentThreadId();
    HDESK hDesktop = GetThreadDesktop(dwThreadID);
    std::vector<HWND> windows;
    EnumDesktopWindows(hDesktop, _find_window_begins_with_cb, reinterpret_cast<LPARAM>(&windows));

    // check window titles
    char wnd_title[256];
    for (HWND hWnd : windows) {
        GetWindowText(hWnd, wnd_title, sizeof(wnd_title));
        if (string_begins_with(std::string(wnd_title), title)) {
            return hWnd;
        }
    }

    // nothing found
    return nullptr;
}

static inline HWND FindProcessWindowBeginsWith(const std::string &title) {
    // try foreground window first
    HWND fg_win = GetForegroundWindow();
    if (string_begins_with(get_window_title(fg_win), title)) {
        DWORD fg_pid;
        GetWindowThreadProcessId(fg_win, &fg_pid);
        if (fg_pid == GetCurrentProcessId()) {
            return fg_win;
        }
    }

    // try different windows
    for (const auto window : find_windows_beginning_with(title)) {
        DWORD fg_pid;
        GetWindowThreadProcessId(window, &fg_pid);
        if (fg_pid == GetCurrentProcessId()) {
            return window;
        }
    }
    return nullptr;
}

static inline std::string get_last_error_string() {

    // get error
    DWORD error = GetLastError();
    if (error == 0) {
        return std::string();
    }

    // get error string
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                 FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr,
                                 error,
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR)&messageBuffer,
                                 0,
                                 nullptr);

    // return as string
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);

    return message;
}

std::wstring s2ws(const std::string &str);
std::string ws2s(const std::wstring &wstr);

static inline std::string guid2s(const GUID guid) {
    return fmt::format("{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}

bool acquire_shutdown_privs();

void generate_ea_card(char card[17]);

static inline int get_async_primary_mouse() {
    int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    return GetAsyncKeyState(vk);
}

static inline int get_async_secondary_mouse() {
    int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON;
    return GetAsyncKeyState(vk);
}

static inline bool parse_width_height(const std::string wh, std::pair<uint32_t, uint32_t> &result) {
    std::string s = wh;
    uint32_t w, h;
    const auto remove_spaces = [](const char& c) { return c == ' '; };
    s.erase(std::remove_if(s.begin(), s.end(), remove_spaces), s.end());
    if (sscanf(s.c_str(), "%u,%u", &w, &h) == 2) {
        result = std::pair(w, h);
        return true;
    } else {
        return false;
    }
}