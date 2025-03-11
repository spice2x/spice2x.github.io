#include "game.h"

/////////////////////////
/// Private Functions ///
/////////////////////////

// add button items
void Game::addItem(Button button) {
    this->buttons.push_back(std::move(button));
}

// add analog items
void Game::addItem(Analog analog) {
    this->analogs.push_back(std::move(analog));
}

// add light items
void Game::addItem(Light light) {
    this->lights.push_back(std::move(light));
}

// add option items
void Game::addItem(Option option) {
    this->options.push_back(std::move(option));
}
