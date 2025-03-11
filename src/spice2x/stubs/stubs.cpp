#include <windows.h>

#include "hooks/libraryhook.h"
#include "util/logging.h"

#ifndef STUB
#include <mutex>

#include "avs/game.h"
#include "launcher/signal.h"
#endif

#include "stubs.h"


/*
 * export/import macros
 */

#if defined(STUB)
#define EXPORT __declspec(dllexport)
#define IMPORT
#else
// needs to be __cdecl if we hook stuff at runtime
#define EXPORT
#define IMPORT
#endif

/*
 * implementations
 */

extern "C" int EXPORT __cdecl bt_fcheck_finish() {
    return 0;
}

extern "C" int EXPORT __cdecl bt_fcheck_init(bool spawn_thread, bool quick_check) {
    return 0;
}

extern "C" int EXPORT __cdecl bt_fcheck_main(void *a1) {
    return 0;
}

extern "C" int EXPORT __cdecl bt_get_ikey_status(stubs::ikey_status* status) {
#ifndef STUB
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_info("kbt", "bt_get_ikey_status");
    });
#endif

    memset(status, 0, 32);
    for (int i = 0; i < 2; i++) {
        status[i].v2 = 1;
        status[i].v4 = 1;
        status[i].v7 = ~0u;

#ifndef STUB
        // JMA is weird in that it expects this to be equal *exactly* to one
        if (avs::game::is_model("JMA")) {
            status[i].v7 = 1;
        }
#endif
    }

#ifndef STUB
    // JMA is weird in that it will keep cycling backup data initialization if this field is not 0
    if (avs::game::is_model("JMA")) {
        status[1].v2 = 0;
    }
#endif

    return 0;
}

extern "C" int EXPORT __cdecl k_bt0001(stubs::ikey_status* status) {
    return bt_get_ikey_status(status);
}

extern "C" void EXPORT __cdecl k_bt0002(void (__cdecl *func)(void *), void *arg) {
    if (func != nullptr) {
        func(arg);
    }
}

/*
 * DLL entry function
 * for standalone stubs
 */
#ifdef STUB
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    DisableThreadLibraryCalls(hinstDLL);

    return TRUE;
}
#endif

/*
 * Dynamic hooks for use without kbt/kld DLL stubs
 * Should be called after all game DLLs are loaded in memory.
 * This works because most (all?) games use GetModuleHandle for those two.
 */
#ifndef STUB
void stubs::attach() {
    log_info("stubs", "attaching...");

    libraryhook_hook_library("kbt.dll", GetModuleHandle(NULL));
    libraryhook_hook_library("kld.dll", GetModuleHandle(NULL));
    libraryhook_hook_proc("bt_fcheck_finish", bt_fcheck_finish);
    libraryhook_hook_proc("bt_fcheck_init", bt_fcheck_init);
    libraryhook_hook_proc("bt_fcheck_main", bt_fcheck_main);
    libraryhook_hook_proc("bt_get_ikey_status", bt_get_ikey_status);
    libraryhook_hook_proc("k_bt0001", k_bt0001);
    libraryhook_hook_proc("k_bt0002", k_bt0002);
    libraryhook_enable();

    log_info("stubs", "attached");
}
#endif
