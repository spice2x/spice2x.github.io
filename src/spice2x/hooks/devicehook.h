#pragma once

#include <windows.h>
#include <string>

namespace hooks::device {
    extern bool ENABLE;
}

extern bool DEVICE_CREATEFILE_DEBUG;

class CustomHandle {
public:
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool com_pass = false;

    virtual ~CustomHandle() = default;

    virtual bool open(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
        return open(lpFileName);
    }

    virtual bool open(LPCWSTR lpFileName) {
        return false;
    };

    virtual int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
        return -1;
    };

    virtual int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
        return -1;
    };

    virtual int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                          DWORD nOutBufferSize) {
        return -1;
    };

    virtual size_t bytes_available() {
        return 255;
    }

    virtual bool close() {
        return true;
    };

    virtual void file_info(LPBY_HANDLE_FILE_INFORMATION lpFileInformation) {
        memset(lpFileInformation, 0, sizeof(*lpFileInformation));
    };

    DCB comm_state {};
    COMMTIMEOUTS comm_timeouts {};
};

class MITMHandle : public CustomHandle {
protected:
    LPCWSTR lpFileName = L"";
    bool lpFileNameContains = false;
    std::string rec_file = "";

public:
    MITMHandle(LPCWSTR lpFileName, std::string rec_file = "", bool lpFileNameContains = false);

    bool open(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
              LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
              DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) override;
    int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
    int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
    int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                  DWORD nOutBufferSize) override;
    size_t bytes_available() override;
    bool close() override;
};

void devicehook_init(HMODULE module = nullptr);
void devicehook_init_trampoline();
void devicehook_add(CustomHandle *device_handle);
void devicehook_dispose();
