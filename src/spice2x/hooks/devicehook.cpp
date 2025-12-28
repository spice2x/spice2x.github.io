#include "devicehook.h"

#include <vector>

#include "avs/game.h"
#include "util/detour.h"
#include "util/utils.h"

#include <tlhelp32.h>

// std::min
#ifdef min
#undef min
#endif

namespace hooks::device {
    bool ENABLE = true;
}

bool DEVICE_CREATEFILE_DEBUG = false;
static std::string PATH_HARD_CODE_COMPARE = "d:/###-###/contents";

static decltype(ClearCommBreak) *ClearCommBreak_orig = nullptr;
static decltype(ClearCommError) *ClearCommError_orig = nullptr;
static decltype(CloseHandle) *CloseHandle_orig = nullptr;
static decltype(CreateFileA) *CreateFileA_orig = nullptr;
static decltype(CreateFileW) *CreateFileW_orig = nullptr;
static decltype(DeviceIoControl) *DeviceIoControl_orig = nullptr;
static decltype(EscapeCommFunction) *EscapeCommFunction_orig = nullptr;
static decltype(GetCommState) *GetCommState_orig = nullptr;
static decltype(GetFileSize) *GetFileSize_orig = nullptr;
static decltype(GetFileSizeEx) *GetFileSizeEx_orig = nullptr;
static decltype(GetFileInformationByHandle) *GetFileInformationByHandle_orig = nullptr;
static decltype(PurgeComm) *PurgeComm_orig = nullptr;
static decltype(ReadFile) *ReadFile_orig = nullptr;
static decltype(SetupComm) *SetupComm_orig = nullptr;
static decltype(SetCommBreak) *SetCommBreak_orig = nullptr;
static decltype(SetCommMask) *SetCommMask_orig = nullptr;
static decltype(SetCommState) *SetCommState_orig = nullptr;
static decltype(SetCommTimeouts) *SetCommTimeouts_orig = nullptr;
static decltype(WriteFile) *WriteFile_orig = nullptr;

static std::vector<CustomHandle *> CUSTOM_HANDLES;

MITMHandle::MITMHandle(LPCWSTR lpFileName, std::string rec_file, bool lpFileNameContains) {
    this->lpFileName = lpFileName;
    this->rec_file = rec_file;
    this->lpFileNameContains = lpFileNameContains;
    this->com_pass = true;
}

bool MITMHandle::open(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {

    // check if file name matches
    if (this->lpFileNameContains) {
        if (wcsstr(lpFileName, this->lpFileName) == 0)
            return false;
    } else {
        if (wcscmp(lpFileName, this->lpFileName))
            return false;
    }

    // create our own file handle
    handle = CreateFileW_orig(lpFileName, dwDesiredAccess, dwShareMode,
                         lpSecurityAttributes, dwCreationDisposition,
                         dwFlagsAndAttributes, hTemplateFile);

    // check if it worked - if not device hook will try again without us
    return handle != INVALID_HANDLE_VALUE;
}

int MITMHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    DWORD lpNumberOfBytesRead = 0;
    auto res = ReadFile_orig(handle, lpBuffer, nNumberOfBytesToRead,
            &lpNumberOfBytesRead, NULL);
    if (res) {

        // record
        log_info("mitm", "read: {}", bin2hex((uint8_t*) lpBuffer, lpNumberOfBytesRead));

        // pass
        return lpNumberOfBytesRead;
    } else {
        return -1;
    }
}

int MITMHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    DWORD lpNumberOfBytesWritten = 0;
    auto res = WriteFile_orig(handle, lpBuffer, nNumberOfBytesToWrite,
            &lpNumberOfBytesWritten, NULL);
    if (res) {

        // record
        log_info("mitm", "write: {}", bin2hex((uint8_t*) lpBuffer, lpNumberOfBytesWritten));

        // pass
        return lpNumberOfBytesWritten;
    } else {
        return -1;
    }
}

int MITMHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer,
        DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    DWORD lpBytesReturned = 0;
    auto res = DeviceIoControl_orig(handle, dwIoControlCode, lpInBuffer, nInBufferSize,
            lpOutBuffer, nOutBufferSize, &lpBytesReturned, NULL);
    if (res) {

        // record
        log_info("mitm", "device_io");

        return lpBytesReturned;
    } else {
        return -1;
    }
}

size_t MITMHandle::bytes_available() {
    COMSTAT status;
    ClearCommError_orig(handle, NULL, &status);
    return status.cbInQue;
}

bool MITMHandle::close() {
    return CloseHandle_orig(handle);
}

static inline CustomHandle *get_custom_handle(HANDLE handle) {

    // TODO: we can make a custom allocator for the handles and
    //       add a simple range check instead of going through the
    //       whole list each time

    // find handle in list
    for (auto custom_handle : CUSTOM_HANDLES) {
        if (reinterpret_cast<HANDLE>(custom_handle) == handle
        || custom_handle->handle == handle) {
            return custom_handle;
        }
    }

    // no handle found - hooks will call original functions for this
    return nullptr;
}

static HANDLE WINAPI CreateFileA_hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    HANDLE result = INVALID_HANDLE_VALUE;

    // convert to wide char
    WCHAR lpFileNameW[512] { 0 };
    if (!MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, lpFileNameW, std::size(lpFileNameW))) {
        return result;
    }

    // debug
    if (DEVICE_CREATEFILE_DEBUG && lpFileName != nullptr) {
        log_info("devicehook", "CreateFileA(\"{}\") => len: {}", lpFileName, strlen(lpFileName));
    }

    // check custom handles
    if (!CUSTOM_HANDLES.empty()) {
        for (auto handle : CUSTOM_HANDLES) {
            if (handle->open(lpFileNameW, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                             dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)) {
                SetLastError(0);
                if (handle->handle != INVALID_HANDLE_VALUE) {
                    result = handle->handle;
                } else {
                    result = (HANDLE) handle;
                }
                break;
            }
        }
    }

    // hard coded paths fix
    auto lpFileNameLen = wcslen(lpFileNameW);
    bool fix = true;
    for (size_t i = 0, c = 0; i < lpFileNameLen && (c = PATH_HARD_CODE_COMPARE[i]) != 0; i++) {
        if (c != '#' && lpFileName[i] != (wchar_t) PATH_HARD_CODE_COMPARE[i]) {
            fix = false;
            break;
        }
    }

    // do the fix
    if (fix && lpFileNameLen >= PATH_HARD_CODE_COMPARE.size()) {
        auto hcLen = PATH_HARD_CODE_COMPARE.size();
        auto buffer = std::make_unique<char[]>(lpFileNameLen + 1);

        buffer[0] = '.';

        for (size_t i = 0; i < lpFileNameLen - hcLen; i++) {
            buffer[i + 1] = lpFileName[hcLen + i];
        }

        if (DEVICE_CREATEFILE_DEBUG) {
            log_info("devicehook", "CreateFileA (fix): {}", buffer.get());
        }

        return CreateFileA_orig(buffer.get(), dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    // fallback
    if (result == INVALID_HANDLE_VALUE) {
        result = CreateFileA_orig(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                  dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    // return result
    return result;
}

static HANDLE WINAPI CreateFileW_hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HANDLE result = INVALID_HANDLE_VALUE;

    // debug
    if (DEVICE_CREATEFILE_DEBUG && lpFileName != nullptr) {
        log_info("devicehook", "CreateFileW: {}", ws2s(lpFileName));
    }

    // check custom handles
    if (!CUSTOM_HANDLES.empty()) {
        for (auto handle : CUSTOM_HANDLES) {
            if (handle->open(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                             dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)) {
                SetLastError(0);
                if (handle->handle != INVALID_HANDLE_VALUE) {
                    result = handle->handle;
                } else {
                    result = (HANDLE) handle;
                }
                break;
            }
        }
    }

    // hard coded paths fix
    bool fix = true;
    auto lpFileNameLen = wcslen(lpFileName);
    for (size_t i = 0, c = 0; i < lpFileNameLen && (c = PATH_HARD_CODE_COMPARE[i]) != 0; i++) {
        if (c != '#' && lpFileName[i] != (wchar_t) PATH_HARD_CODE_COMPARE[i]) {
            fix = false;
            break;
        }
    }

    // do the fix
    if (fix && lpFileNameLen >= PATH_HARD_CODE_COMPARE.size()) {
        auto hcLen = PATH_HARD_CODE_COMPARE.size();
        auto buffer = std::make_unique<wchar_t[]>(lpFileNameLen + 1);

        buffer[0] = '.';

        for (size_t i = 0; i < lpFileNameLen - hcLen; i++) {
            buffer[i + 1] = lpFileName[hcLen + i];
        }

        if (DEVICE_CREATEFILE_DEBUG) {
            log_info("devicehook", "CreateFileW (fix): {}", ws2s(buffer.get()));
        }

        return CreateFileW_orig(buffer.get(), dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    // fallback
    if (result == INVALID_HANDLE_VALUE) {
        result = CreateFileW_orig(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                  dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    // return result
    return result;
}

static BOOL WINAPI ReadFile_hook(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                                 LPOVERLAPPED lpOverlapped)
{
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle) {
        int value = custom_handle->read(lpBuffer, nNumberOfBytesToRead);
        if (value >= 0) {
            SetLastError(0);
            *lpNumberOfBytesRead = (DWORD) value;
            return true;
        } else {
            SetLastError(0xD);
            return false;
        }
    }

    // fallback
    return ReadFile_orig(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

static BOOL WINAPI WriteFile_hook(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                                  LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle) {
        int value = custom_handle->write(lpBuffer, nNumberOfBytesToWrite);
        if (value >= 0) {
            SetLastError(0);
            *lpNumberOfBytesWritten = (DWORD) value;
            return true;
        } else {
            SetLastError(0xD);
            return false;
        }
    }

    // fallback
    return WriteFile_orig(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

static BOOL WINAPI DeviceIoControl_hook(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                        LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned,
                                        LPOVERLAPPED lpOverlapped)
{
    auto *custom_handle = get_custom_handle(hDevice);
    if (custom_handle) {
        int count = custom_handle->device_io(dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize);
        if (count >= 0) {
            SetLastError(0);
            *lpBytesReturned = (DWORD) count;
            if (lpOverlapped) {
                SetEvent(lpOverlapped->hEvent);
            }
            return true;
        } else {
            log_info("devicehook", "device_io failed");
            SetLastError(0xD);
            return false;
        }
    }

    // fallback
    return DeviceIoControl_orig(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize,
                                lpBytesReturned, lpOverlapped);
}

static DWORD WINAPI GetFileSize_hook(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    //log_info("devicehook", "GetFileSizeHook hit");
    return GetFileSize_orig(hFile, lpFileSizeHigh);
}

static BOOL WINAPI GetFileSizeEx_hook(HANDLE hFile, PLARGE_INTEGER lpFileSizeHigh) {
    //log_info("devicehook", "GetFileSizeExHook hit");
    return GetFileSizeEx_orig(hFile, lpFileSizeHigh);
}

static BOOL WINAPI GetFileInformationByHandle_hook(HANDLE hFile,
        LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
    // custom handle
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle) {
        SetLastError(0);
        custom_handle->file_info(lpFileInformation);
        return TRUE;
    }

    return GetFileInformationByHandle_orig(hFile, lpFileInformation);
}

static BOOL WINAPI ClearCommBreak_hook(HANDLE hFile) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        return TRUE;
    }

    return ClearCommBreak_orig(hFile);
}

static BOOL WINAPI ClearCommError_hook(HANDLE hFile, LPDWORD lpErrors, LPCOMSTAT lpStat) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        if (lpStat) {
            lpStat->fXoffSent = 1;

            /*
             * Some games may check the input queue size.
             * QMA does not even attempt to read if this is set to 0.
             * We just set this to 255 and hope games do not rely on this for buffer sizes.
             *
             * Message from the future: As it turned out, some games (CCJ) do in fact rely on this value.
             */
            lpStat->cbInQue = custom_handle->bytes_available();
        }
        if (lpErrors) {
            // gitadora delta needs this, or else
            // the io will keep spamming 0xAA
            *lpErrors = 0;
        }

        return TRUE;
    }

    return ClearCommError_orig(hFile, lpErrors, lpStat);
}

static BOOL WINAPI EscapeCommFunction_hook(HANDLE hFile, DWORD dwFunc) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        return TRUE;
    }

    return EscapeCommFunction_orig(hFile, dwFunc);
}

static BOOL WINAPI GetCommState_hook(HANDLE hFile, LPDCB lpDCB) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        auto *comm_state = &custom_handle->comm_state;

        memcpy(lpDCB, comm_state, std::min(static_cast<size_t>(comm_state->DCBlength), sizeof(*comm_state)));

        return TRUE;
    }

    return GetCommState_orig(hFile, lpDCB);
}

static BOOL WINAPI PurgeComm_hook(HANDLE hFile, DWORD dwFlags) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        return TRUE;
    }

    return PurgeComm_orig(hFile, dwFlags);
}

static BOOL WINAPI SetupComm_hook(HANDLE hFile, DWORD dwInQueue, DWORD dwOutQueue) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        return TRUE;
    }

    return SetupComm_orig(hFile, dwInQueue, dwOutQueue);
}

static BOOL WINAPI SetCommBreak_hook(HANDLE hFile) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        return TRUE;
    }

    return SetCommBreak_orig(hFile);
}

static BOOL WINAPI SetCommMask_hook(HANDLE hFile, DWORD dwEvtMask) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        return TRUE;
    }

    return SetCommMask_orig(hFile, dwEvtMask);
}

static BOOL WINAPI SetCommState_hook(HANDLE hFile, LPDCB lpDCB) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle) {

        // sanity check
        if (lpDCB->DCBlength <= sizeof(custom_handle->comm_state)) {
            memcpy(&custom_handle->comm_state, lpDCB, lpDCB->DCBlength);
        }

        return TRUE;
    }

    return SetCommState_orig(hFile, lpDCB);
}

static BOOL WINAPI SetCommTimeouts_hook(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts) {
    auto *custom_handle = get_custom_handle(hFile);
    if (custom_handle && !custom_handle->com_pass) {
        memcpy(&custom_handle->comm_timeouts, lpCommTimeouts, sizeof(custom_handle->comm_timeouts));
        return TRUE;
    }

    return SetCommTimeouts_orig(hFile, lpCommTimeouts);
}

static BOOL WINAPI CloseHandle_hook(HANDLE hObject) {
    auto *custom_handle = get_custom_handle(hObject);
    if (custom_handle) {
        SetLastError(0);
        return custom_handle->close();
    }

    // call original
    return CloseHandle_orig(hObject);
}

static void suspend_or_resume_other_threads(bool suspending) {
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) {
        return;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);

    if (Thread32First(hThreadSnap, &te32)) {
        do {
            if (te32.th32OwnerProcessID == GetCurrentProcessId()) {
                if(te32.th32ThreadID == GetCurrentThreadId()) {
                    continue;
                }

                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
                if (hThread) {
                    if (suspending) {
                        SuspendThread(hThread);
                    } else {
                        ResumeThread(hThread);
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hThreadSnap, &te32));
    }

    CloseHandle(hThreadSnap);
}

void devicehook_init(HMODULE module) {
    if (!hooks::device::ENABLE) {
        return;
    }

#define STORE(value, expr) { \
    auto tmp = (expr); \
    if ((value) == nullptr) { \
        (value) = tmp; \
    } \
}

    /*
    // initialize only once
    static bool initialized = false;
    if (initialized) {
        return;
    } else {
        initialized = true;
    }
    */

    log_info("devicehook", "init");

    suspend_or_resume_other_threads(true);

    // IAT hooks
    STORE(ClearCommBreak_orig, detour::iat_try("ClearCommBreak", ClearCommBreak_hook, module));
    STORE(ClearCommError_orig, detour::iat_try("ClearCommError", ClearCommError_hook, module));
    STORE(CloseHandle_orig, detour::iat_try("CloseHandle", CloseHandle_hook, module));
    STORE(CreateFileA_orig, detour::iat_try("CreateFileA", CreateFileA_hook, module));
    STORE(CreateFileW_orig, detour::iat_try("CreateFileW", CreateFileW_hook, module));
    STORE(DeviceIoControl_orig, detour::iat_try("DeviceIoControl", DeviceIoControl_hook, module));
    STORE(EscapeCommFunction_orig, detour::iat_try("EscapeCommFunction", EscapeCommFunction_hook, module));
    STORE(GetCommState_orig, detour::iat_try("GetCommState", GetCommState_hook, module));
    STORE(GetFileSize_orig, detour::iat_try("GetFileSize", GetFileSize_hook, module));
    STORE(GetFileSizeEx_orig, detour::iat_try("GetFileSize", GetFileSizeEx_hook, module));
    STORE(GetFileInformationByHandle_orig, detour::iat_try(
                "GetFileInformationByHandle", GetFileInformationByHandle_hook, module));
    STORE(PurgeComm_orig, detour::iat_try("PurgeComm", PurgeComm_hook, module));
    STORE(ReadFile_orig, detour::iat_try("ReadFile", ReadFile_hook, module));
    STORE(SetupComm_orig, detour::iat_try("SetupComm", SetupComm_hook, module));
    STORE(SetCommBreak_orig, detour::iat_try("SetCommBreak", SetCommBreak_hook, module));
    STORE(SetCommMask_orig, detour::iat_try("SetCommMask", SetCommMask_hook, module));
    STORE(SetCommState_orig, detour::iat_try("SetCommState", SetCommState_hook, module));
    STORE(SetCommTimeouts_orig, detour::iat_try("SetCommTimeouts", SetCommTimeouts_hook, module));
    STORE(WriteFile_orig, detour::iat_try("WriteFile", WriteFile_hook, module));

    suspend_or_resume_other_threads(false);

#undef STORE
}

void devicehook_init_trampoline() {
    // initialize only once
    static bool initialized = false;
    if (initialized) {
        return;
    } else {
        initialized = true;
    }

    suspend_or_resume_other_threads(true);

    detour::trampoline_try("kernel32.dll", "ClearCommBreak", ClearCommBreak_hook, &ClearCommBreak_orig);
    detour::trampoline_try("kernel32.dll", "ClearCommError", ClearCommError_hook, &ClearCommError_orig);
    detour::trampoline_try("kernel32.dll", "CloseHandle", CloseHandle_hook, &CloseHandle_orig);
    detour::trampoline_try("kernel32.dll", "CreateFileA", CreateFileA_hook, &CreateFileA_orig);
    detour::trampoline_try("kernel32.dll", "CreateFileW", CreateFileW_hook, &CreateFileW_orig);
    detour::trampoline_try("kernel32.dll", "DeviceIoControl", DeviceIoControl_hook, &DeviceIoControl_orig);
    detour::trampoline_try("kernel32.dll", "EscapeCommFunction", EscapeCommFunction_hook, &EscapeCommFunction_orig);
    detour::trampoline_try("kernel32.dll", "WriteFile", WriteFile_hook, &WriteFile_orig);
    detour::trampoline_try("kernel32.dll", "GetFileSize", GetFileSize_hook, &GetFileSize_orig);
    detour::trampoline_try("kernel32.dll", "GetFileSizeEx", GetFileSizeEx_hook, &GetFileSizeEx_orig);
    detour::trampoline_try("kernel32.dll", "GetFileInformationByHandle",
                           GetFileInformationByHandle_hook, &GetFileInformationByHandle_orig);
    detour::trampoline_try("kernel32.dll", "GetCommState", GetCommState_hook, &GetCommState_orig);
    detour::trampoline_try("kernel32.dll", "PurgeComm", PurgeComm_hook, &PurgeComm_orig);
    detour::trampoline_try("kernel32.dll", "ReadFile", ReadFile_hook, &ReadFile_orig);
    detour::trampoline_try("kernel32.dll", "SetupComm", SetupComm_hook, &SetupComm_orig);
    detour::trampoline_try("kernel32.dll", "SetCommBreak", SetCommBreak_hook, &SetCommBreak_orig);
    detour::trampoline_try("kernel32.dll", "SetCommMask", SetCommMask_hook, &SetCommMask_orig);
    detour::trampoline_try("kernel32.dll", "SetCommState", SetCommState_hook, &SetCommState_orig);
    detour::trampoline_try("kernel32.dll", "SetCommTimeouts", SetCommTimeouts_hook, &SetCommTimeouts_orig);

    suspend_or_resume_other_threads(false);
}

void devicehook_add(CustomHandle *device_handle) {
    CUSTOM_HANDLES.push_back(device_handle);
}

void devicehook_dispose() {

    // clean up custom handles
    for (auto handle : CUSTOM_HANDLES) {
        delete handle;
    }
    CUSTOM_HANDLES.clear();
}
