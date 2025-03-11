#include "resutils.h"
#include "util/utils.h"

const char *resutil::load_file(int name, DWORD *size, LPCSTR type) {
    HMODULE handle = GetModuleHandle(NULL);
    HRSRC rc = FindResource(handle, MAKEINTRESOURCE(name), type);
    HGLOBAL rcData = LoadResource(handle, rc);
    if (size != nullptr)
        *size = SizeofResource(handle, rc);
    return static_cast<const char*>(LockResource(rcData));
}

std::string resutil::load_file_string(int name, LPCSTR type) {
    DWORD size = 0;
    auto data = resutil::load_file(name, &size, type);
    return std::string(data, size);
}

std::string resutil::load_file_string_crlf(int name, LPCSTR type) {
    std::string s = load_file_string(name, type);
    strreplace(s, "\n", "\r\n");
    return s;
}
