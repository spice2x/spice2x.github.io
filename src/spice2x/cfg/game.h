#pragma once

#include <vector>
#include <typeinfo>
#include <iostream>

#include "button.h"
#include "analog.h"
#include "light.h"
#include "option.h"
#include "external/tinyxml2/tinyxml2.h"

class Game {
public:
    explicit Game(std::string game_name) : game_name(std::move(game_name)) {};

    /*
    template<typename T, typename... Rest>
    Game(std::string gameName) : gameName(std::move(gameName)) {};
    */

    ~Game() = default;

    inline const std::string &getGameName() const {
        return this->game_name;
    };

    inline std::vector<std::string> &getDLLNames() {
        return this->dll_names;
    }

    inline void addDLLName(std::string dll_name) {
        this->dll_names.push_back(std::move(dll_name));
    }

    template<typename T>
    void addItems(T t) {
        this->addItem(t);
    }

    template<typename T, typename... Rest>
    void addItems(T t, Rest... rest) {
        this->addItem(t);
        this->addItems(rest...);
    }

    inline std::vector<Button> &getButtons() {
        return this->buttons;
    }
    inline const std::vector<Button> &getButtons() const {
        return this->buttons;
    }
    inline std::vector<Analog> &getAnalogs() {
        return this->analogs;
    }
    inline const std::vector<Analog> &getAnalogs() const {
        return this->analogs;
    }
    inline std::vector<Light> &getLights() {
        return this->lights;
    }
    inline const std::vector<Light> &getLights() const {
        return this->lights;
    }
    inline std::vector<Option> &getOptions() {
        return this->options;
    }
    inline const std::vector<Option> &getOptions() const {
        return this->options;
    }

private:
    std::string game_name;
    std::vector<std::string> dll_names;
    std::vector<Button> buttons;
    std::vector<Analog> analogs;
    std::vector<Light> lights;
    std::vector<Option> options;

    void addItem(Button button);
    void addItem(Analog analog);
    void addItem(Light light);
    void addItem(Option option);
};
