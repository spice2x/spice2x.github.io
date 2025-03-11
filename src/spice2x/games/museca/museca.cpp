#include "museca.h"

#include "avs/game.h"
#include "games/shared/lcdhandle.h"
#include "hooks/devicehook.h"

namespace games::museca {

    MusecaGame::MusecaGame() : Game("Museca") {
    }

    void MusecaGame::attach() {
        Game::attach();

        devicehook_init();
        devicehook_add(new games::shared::LCDHandle());
    }
}
