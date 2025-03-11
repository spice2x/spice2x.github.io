#include "ccj.h"

#include <format>
#include "util/libutils.h"
#include "util/fileutils.h"
#include "util/unity_player.h"
#include "util/utils.h"
#include "util/detour.h"
#include "acioemu/handle.h"
#include "misc/wintouchemu.h"
#include "hooks/graphics/graphics.h"
#include "bi2x_hook.h"
#include "trackball.h"

namespace games::ccj {
    // nomatchselect: disables debug menu that says "Select Match Mode"
    // q1: disables v-sync
    std::string CCJ_INJECT_ARGS = "-nomatchselect";

    static acioemu::ACIOHandle *acioHandle = nullptr;
    static std::string commandLine;

    static decltype(AddVectoredExceptionHandler) *AddVectoredExceptionHandler_orig = nullptr;
    static decltype(CreateFileW) *execexe_CreateFileW_orig = nullptr;
    static decltype(ShowCursor) *ShowCursor_orig = nullptr;
    static decltype(GetCommandLineA) *GetCommandLineA_orig = nullptr;

    static HANDLE WINAPI execexe_CreateFileW_hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                                  LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                                  DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
        const auto fileName = ws2s(lpFileName);
        if (fileName == "COM1" && acioHandle->open(lpFileName)) {
            SetLastError(0);
            return (HANDLE) acioHandle;
        } else {
            return execexe_CreateFileW_orig(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                            dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        }
    }

    static int WINAPI ShowCursor_hook(BOOL bShow) {
        return 1;
    }

    static PVOID WINAPI AddVectoredExceptionHandler_hook(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
        return INVALID_HANDLE_VALUE;
    }

    static LPSTR WINAPI GetCommandLineA_hook() {
        return (LPSTR)commandLine.c_str();
    }

    void CCJGame::attach() {
        Game::attach();

        // create required files
        fileutils::dir_create_recursive("dev/raw/log");
        fileutils::bin_write("dev/raw/bootio", nullptr, 0);
        fileutils::bin_write("dev/raw/log/output_log.txt", nullptr, 0);

        // preload libraries
        libutils::load_library("execexe.dll");
        libutils::load_library("libaio.dll");
        libutils::load_library("libaio-iob.dll");
        libutils::load_library("libaio-iob2_video.dll");
        libutils::load_library("win10actlog.dll");

        detour::trampoline_try("execexe.dll", MAKEINTRESOURCE(11),
                               (void*)execexe_CreateFileW_hook,(void**)&execexe_CreateFileW_orig);

        // insert BI2X hooks
        bi2x_hook_init();

        // insert trackball hooks
        trackball_hook_init();

        // add card reader
        acioHandle = new acioemu::ACIOHandle(L"COM1");
        devicehook_init_trampoline();
        devicehook_add(acioHandle);
    }

    void CCJGame::post_attach() {
        Game::post_attach();

        detour::trampoline_try("kernel32.dll", "AddVectoredExceptionHandler",
                               (void*)AddVectoredExceptionHandler_hook,(void**)&AddVectoredExceptionHandler_orig);
        detour::trampoline_try("kernel32.dll", "GetCommandLineA",
                               (void*)GetCommandLineA_hook, (void**)&GetCommandLineA_orig);

        if (GRAPHICS_SHOW_CURSOR) {
            detour::trampoline_try("user32.dll", "ShowCursor",
                                   (void*)ShowCursor_hook, (void**)&ShowCursor_orig);
        }

        commandLine =
            std::format("{} {}{}",
            GetCommandLineA_orig(),
            CCJ_INJECT_ARGS,
            unity_utils::get_unity_player_args());
        log_info("ccj", "unity player args: ```{}```", commandLine);

        trackball_thread_start();
    }

    void CCJGame::detach() {
        Game::detach();

        devicehook_dispose();
        trackball_thread_stop();
    }
}