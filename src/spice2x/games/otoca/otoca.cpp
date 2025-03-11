#include "otoca.h"
#include "p4io.h"
#include "games/shared/printer.h"
#include "util/libutils.h"

namespace games::otoca {

    OtocaGame::OtocaGame() : Game("Otoca D'or") {
    }

    OtocaGame::~OtocaGame() {
    }

    void OtocaGame::attach() {
        Game::attach();

        /*
         * arkkep.dll uses LoadLibrary to access game.dll
         * we preload it so that hooks (e.g. for graphics) will work
         */
        libutils::try_library("game.dll");

        // initialize IO hooks
        p4io_hook();
        games::shared::printer_attach();
    }
}
