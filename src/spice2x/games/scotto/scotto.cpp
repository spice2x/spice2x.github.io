#include "scotto.h"

#include <windows.h>

#include "acio/icca/icca.h"
#include "games/game.h"

namespace games::scotto {

    ScottoGame::ScottoGame() : Game("Scotto") {
    }

    void ScottoGame::attach() {
        Game::attach();

        // scotto has a flipped numpad
        acio::ICCA_FLIP_ROWS = !acio::ICCA_FLIP_ROWS;
    }

    void ScottoGame::detach() {
        Game::detach();
    }
}
