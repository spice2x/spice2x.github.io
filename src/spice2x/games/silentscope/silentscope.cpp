#include "silentscope.h"

#include "cfg/configurator.h"
#include "hooks/devicehook.h"
#include "util/libutils.h"

#include "projector.h"

namespace games::silentscope {

    SilentScopeGame::SilentScopeGame() : Game("Silent Scope") {
    }

    void SilentScopeGame::attach() {
        Game::attach();

        // load the game DLL so hooks apply
        libutils::try_library("gamendd.dll");

        devicehook_init();
        devicehook_add(new ProjectorHandle());
    }

    void SilentScopeGame::detach() {
        Game::detach();
    }
}
