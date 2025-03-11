#pragma once

#include <windows.h>
#include <string>

namespace resutil {

    const char* load_file(int name, DWORD* size, LPCSTR type=RT_RCDATA);
    std::string load_file_string(int name, LPCSTR type=RT_RCDATA);
    std::string load_file_string_crlf(int name, LPCSTR type=RT_RCDATA);
}
