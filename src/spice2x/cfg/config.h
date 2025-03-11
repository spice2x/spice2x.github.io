#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>

#include "external/tinyxml2/tinyxml2.h"

#include "game.h"

// settings
extern std::string CONFIG_PATH_OVERRIDE;

struct ConfigKeypadBindings {
    std::string keypads[2];
    std::filesystem::path card_paths[2];
};

class Config {
public:
    static Config &getInstance();
    bool getStatus();
    bool createConfigFile();

    bool addGame(Game &game);

    bool updateBinding(const Game &game, const Button &button, int alternative);
    bool updateBinding(const Game &game, const Analog &analog);
    bool updateBinding(const Game &game, ConfigKeypadBindings &keypads);
    bool updateBinding(const Game &game, const Light &light, int alternative);
    bool updateBinding(const Game &game, const Option &option);

    std::vector<Button> getButtons(const std::string &game);
    std::vector<Button> getButtons(Game *);

    std::vector<Analog> getAnalogs(const std::string &game);
    std::vector<Analog> getAnalogs(Game *);

    ConfigKeypadBindings getKeypadBindings(const std::string &game);
    ConfigKeypadBindings getKeypadBindings(Game *);

    std::vector<Light> getLights(const std::string &game);
    std::vector<Light> getLights(Game *);

    std::vector<Option> getOptions(const std::string &game);
    std::vector<Option> getOptions(Game *);

private:
    Config();
    Config(const Config &);
    ~Config() = default;

    const Config &operator=(const Config &);

    tinyxml2::XMLDocument configFile;
    bool status;
    std::filesystem::path configLocation;
    std::filesystem::path configLocationTemp;

    bool firstFillConfigFile();
    void saveConfigFile();
};
