#include "rf3d.h"

#include "avs/game.h"
#include "hooks/devicehook.h"
#include "util/detour.h"

games::rf3d::RF3DGame::RF3DGame() : Game("Road Fighters 3D") {
}

static decltype(GetFileAttributesA)* GetFileAttributesA_orig;

static DWORD __stdcall GetFileAttributesA_hook(LPCSTR lpFileName) {

    if (strcmp(lpFileName, "C:\\WERUNTIME.INI") == 0) {
        return INVALID_FILE_ATTRIBUTES;
    }

    return GetFileAttributesA_orig(lpFileName);
}

void games::rf3d::RF3DGame::attach() {
    Game::attach();

    // device hook for JGT-001 redirection
    devicehook_init();

    // game changes power configurations if C:\WERUNTIME.INI exists
    GetFileAttributesA_orig = detour::iat_try("GetFileAttributesA", GetFileAttributesA_hook, avs::game::DLL_INSTANCE);
}

void games::rf3d::RF3DGame::detach() {
    Game::detach();
    devicehook_dispose();
}
