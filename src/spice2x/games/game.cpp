#include "game.h"
#include "util/logging.h"

games::Game::Game(std::string name) {
    this->name = name;
}

void games::Game::attach() {
    log_info("game", "attach: {}", name);
}

void games::Game::pre_attach() {
    log_info("game", "pre_attach: {}", name);
}

void games::Game::post_attach() {
    log_info("game", "post_attach: {}", name);
}

void games::Game::detach() {
    log_info("game", "detach: {}", name);
}
