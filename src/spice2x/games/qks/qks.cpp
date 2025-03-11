#include "qks.h"

#include <format>
#include "util/libutils.h"
#include "util/fileutils.h"
#include "util/utils.h"
#include "util/unity_player.h"
#include "util/detour.h"
#include "acioemu/handle.h"
#include "hooks/graphics/graphics.h"
#include "bi2x_hook.h"

namespace games::qks {
    std::string QKS_INJECT_ARGS = "";

    const std::wstring portName = L"\\\\.\\COM1";
    static acioemu::ACIOHandle *acioHandle = nullptr;
    static std::string commandLine;
    static bool isOpened = false;

    static decltype(AddVectoredExceptionHandler) *AddVectoredExceptionHandler_orig = nullptr;
    static decltype(ShowCursor) *ShowCursor_orig = nullptr;
    static decltype(CreateFileA) *execexe_CreateFileA_orig = nullptr;
    static decltype(CloseHandle) *execexe_CloseHandle_orig = nullptr;
    static decltype(GetCommandLineA) *GetCommandLineA_orig = nullptr;

    static HANDLE WINAPI execexe_CreateFileA_hook(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                  LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                                  DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
        const auto lpFileNameW = s2ws(lpFileName);
        if (lpFileNameW == portName) {
            if (!isOpened) {
                isOpened = acioHandle->open(portName.c_str());
            } else {
                // NOTE: For some reason, QKS repeatedly opens and closes the COM port.
                // In the existing implementation of `icca.cpp`, the game side would specify an impossible
                // number of units and call `log_fatal()`, causing the game to crash, so the handle is reused.
                log_info("qks", "ignored handle open. ({})", ws2s(portName));
            }
            SetLastError(0);
            return (HANDLE)acioHandle;
        }
        return execexe_CreateFileA_orig(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    static WINBOOL WINAPI execexe_CloseHandle_hook(HANDLE hObject) {
        if (hObject == acioHandle && isOpened) {
            log_info("qks", "ignored handle close. ({})", ws2s(portName));
            return TRUE;
        }
        return execexe_CloseHandle_orig(hObject);
    }

    static int WINAPI ShowCursor_hook(BOOL bShow) {
        return TRUE;
    }

    static PVOID WINAPI AddVectoredExceptionHandler_hook(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
        return INVALID_HANDLE_VALUE;
    }

    static LPSTR WINAPI GetCommandLineA_hook() {
        return (LPSTR)commandLine.c_str();
    }

    void QKSGame::attach() {
        Game::attach();

        // create required files
        fileutils::dir_create_recursive("dev/raw/log");
        fileutils::bin_write("dev/raw/log/output_log.txt", nullptr, 0);

        // create optional files (suppress warning)
        fileutils::bin_write("dev/raw/error.txt", nullptr, 0);

        // preload libraries
        libutils::load_library("execexe.dll");
        libutils::load_library("libaio.dll");
        libutils::load_library("libaio-iob.dll");
        libutils::load_library("libaio-iob2_video.dll");
        libutils::load_library("virtualsurroundnative.dll");

        detour::trampoline_try("execexe.dll", MAKEINTRESOURCE(7),
                               execexe_CloseHandle_hook,&execexe_CloseHandle_orig);
        detour::trampoline_try("execexe.dll", MAKEINTRESOURCE(9),
                               execexe_CreateFileA_hook,&execexe_CreateFileA_orig);

        // insert BI2X hooks
        bi2x_hook_init();

        // add card reader
        acioHandle = new acioemu::ACIOHandle(portName.c_str());
        devicehook_init_trampoline();
        devicehook_add(acioHandle);
    }

    void QKSGame::post_attach() {
        Game::post_attach();

        detour::trampoline_try("kernel32.dll", "AddVectoredExceptionHandler",
                               AddVectoredExceptionHandler_hook, &AddVectoredExceptionHandler_orig);
        detour::trampoline_try("kernel32.dll", "GetCommandLineA",
                               GetCommandLineA_hook, &GetCommandLineA_orig);

        if (GRAPHICS_SHOW_CURSOR) {
            detour::trampoline_try("user32.dll", "ShowCursor",
                                   ShowCursor_hook, &ShowCursor_orig);
        }

        commandLine =
            std::format("{} {}{}",
            GetCommandLineA_orig(),
            QKS_INJECT_ARGS,
            unity_utils::get_unity_player_args());
        log_info("qks", "unity player args: ```{}```", commandLine);
    }

    void QKSGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}