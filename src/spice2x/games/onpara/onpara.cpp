#include "onpara.h"

#include "avs/game.h"
#include "util/detour.h"
#include "util/utils.h"
#include "util/libutils.h"
#include "util/sigscan.h"
#include "hooks/devicehook.h"
#include "hooks/sleephook.h"
#include "hooks/graphics/graphics.h"
#include "touch/touch.h"
#include "touchpanel.h"
#include "westboard.h"

static decltype(BuildCommDCBA)* orig_BuildCommDCBA = BuildCommDCBA;

games::onpara::OnparaGame::OnparaGame() : games::Game("Ongaku Paradise") { }

static BOOL WINAPI my_BuildCommDCBA(LPCSTR lpDef, LPDCB lpDCB) {
    if (lpDef == nullptr || lpDCB == nullptr) {
        return orig_BuildCommDCBA(lpDef, lpDCB);
    }

    log_misc("onpara", "BuildCommDCBA({})", lpDef);

    // remove the device name if present
    if (string_begins_with(lpDef, "COM") && std::strlen(lpDef) >= 5) {
        lpDef += 6;
    } 

    return orig_BuildCommDCBA(lpDef, lpDCB);
}

static int __cdecl setvolume_and_setequalizer_stub(const char*) {
    // log_misc("onpara", "setvolume_and_setequalizer_stub()");
    return 0;
}

void games::onpara::OnparaGame::attach() {
    Game::attach();

    // make the game start faster
    auto gamejc9_module = libutils::try_library("gamejc9.dll");
    if (gamejc9_module != nullptr) {
        hooks::sleep::init(29999, 1, gamejc9_module);
    }

    // attach touch handler
    log_info("onpara", "attach touch handler");
    touch_attach_dx_hook();
    if (GRAPHICS_SHOW_CURSOR) {
        ShowCursor(1);
    }

    // patch the CRC check 
    // TODO: I don't like patching, make this not a patch
    replace_pattern(avs::game::DLL_INSTANCE, "8D4C240851E8????????83C404", "8D4C240851B80000000083C404", 0, 0);

    detour::iat_try("BuildCommDCBA", my_BuildCommDCBA);

    // load this now so that we can hook its dependencies
    libutils::try_library("gamejc9.dll");
    // stub annoying functions
    detour::iat_try("?setvolume@@YAHPAD@Z", setvolume_and_setequalizer_stub); 
    detour::iat_try("SetEqualizer", setvolume_and_setequalizer_stub);

    devicehook_init(avs::game::DLL_INSTANCE);
    devicehook_add(new TouchPanelHandle());
    devicehook_add(new WestBoardHandle());
}

void games::onpara::OnparaGame::detach() {
    Game::detach();
}