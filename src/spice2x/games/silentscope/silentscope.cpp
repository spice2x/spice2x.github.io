#include "silentscope.h"

#include "acioemu/handle.h"
#include "cfg/configurator.h"
#include "hooks/devicehook.h"
#include "util/libutils.h"

namespace games::silentscope {

    SilentScopeGame::SilentScopeGame() : Game("Silent Scope") {
    }

    void SilentScopeGame::attach() {
        Game::attach();

        // load the game DLL so hooks apply
        libutils::try_library("gamendd.dll");

        // TODO(felix): implement stuff on this port
        devicehook_init();
        devicehook_add(new acioemu::ACIOHandle(L"COM2"));
    }

    void SilentScopeGame::detach() {
        Game::detach();
    }
}
