#pragma once

#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include "option.h"

namespace rawinput {
    class RawInputManager;
    struct Device;
}

class Button;
class Analog;
class Light;
class Game;
class Option;

namespace GameAPI {
    namespace Buttons {
        enum State {
            BUTTON_PRESSED = true,
            BUTTON_NOT_PRESSED = false
        };

        /**
         * Parses the config and returns the buttons set by the user.
         *
         * @param a std::string containing the game name OR a pointer to a Game
         * @return a vector pointer containing pointers of Button's set by the user
         */
        std::vector<Button> getButtons(const std::string &game_name);
        std::vector<Button> getButtons(Game *);

        /**
         * Sorts Buttons by their name
         *
         * @param a pointer to a vector pointer of Button pointers that will be sorted by the order of a vector pointer of strings
         *        OR a parameter pack of std::string... can be used.
         * @return void
         */
        std::vector<Button> sortButtons(
                const std::vector<Button> &buttons,
                const std::vector<std::string> &button_names,
                const std::vector<unsigned short> *vkey_defaults = nullptr);

        template<typename T>
        void sortButtons(std::vector<Button> *buttons, T t) {
            const std::vector<std::string> names { t };

            if (buttons) {
                *buttons = GameAPI::Buttons::sortButtons(*buttons, names);
            }
        }

        template<typename T, typename... Rest>
        void sortButtons(std::vector<Button> *buttons, T t, Rest... rest) {
            const std::vector<std::string> names { t, rest... };

            if (buttons) {
                *buttons = GameAPI::Buttons::sortButtons(*buttons, names);
            }
        }

        /**
         * Returns the state of whether a button is pressed or not.
         * Highly recommended to use either of these two functions than the other two below them.
         *
         * @return either a GameAPI::Buttons::State::BUTTON_PRESSED or a Game::API::Buttons::State::BUTTON_NOT_PRESSED
         */
        State getState(rawinput::RawInputManager *manager, Button &button, bool check_alts = true);
        State getState(std::unique_ptr<rawinput::RawInputManager> &manager, Button &button, bool check_alts = true);

        /**
         * Returns the current velocity of a button.
         * When not pressed, the returned velocity is 0.
         * When pressed, the velocity can be anywhere in the range [0, 1]
         * @return velocity in the range [0, 1]
         */
        float getVelocity(rawinput::RawInputManager *manager, Button &button);
        float getVelocity(std::unique_ptr<rawinput::RawInputManager> &manager, Button &button);
    }

    namespace Analogs {
        std::vector<Analog> getAnalogs(const std::string &game_name);

        std::vector<Analog> sortAnalogs(
                const std::vector<Analog> &analogs,
                const std::vector<std::string> &analog_names);

        template<typename T>
        void sortAnalogs(std::vector<Analog> *analogs, T t) {
            const std::vector<std::string> analog_names { t };

            if (analogs) {
                *analogs = GameAPI::Analogs::sortAnalogs(*analogs, analog_names);
            }
        }

        template<typename T, typename... Rest>
        void sortAnalogs(std::vector<Analog> *analogs, T t, Rest... rest) {
            const std::vector<std::string> analog_names { t, rest... };

            if (analogs) {
                *analogs = GameAPI::Analogs::sortAnalogs(*analogs, analog_names);
            }
        }

        float getState(rawinput::Device *device, Analog &analog);
        float getState(rawinput::RawInputManager *manager, Analog &analog);
        float getState(std::unique_ptr<rawinput::RawInputManager> &manager, Analog &analog);
    }

    namespace Lights {
        std::vector<Light> getLights(const std::string &game_name);

        std::vector<Light> sortLights(
                const std::vector<Light> &lights,
                const std::vector<std::string> &light_names);

        template<typename T>
        void sortLights(std::vector<Light> *lights, T t) {
            const std::vector<std::string> light_names { t };

            if (lights) {
                *lights = GameAPI::Lights::sortLights(*lights, light_names);
            }
        }

        template<typename T, typename... Rest>
        void sortLights(std::vector<Light> *lights, T t, Rest... rest) {
            const std::vector<std::string> light_names { t, rest... };

            if (lights) {
                *lights = GameAPI::Lights::sortLights(*lights, light_names);
            }
        }

        void writeLight(rawinput::Device *device, int index, float value);
        void writeLight(rawinput::RawInputManager *manager, Light &light, float value);
        void writeLight(std::unique_ptr<rawinput::RawInputManager> &manager, Light &light, float value);

        float readLight(rawinput::Device *device, int index);
        float readLight(rawinput::RawInputManager *manager, Light &light);
        float readLight(std::unique_ptr<rawinput::RawInputManager> &manager, Light &light);
    }

    namespace Options {
        std::vector<Option> getOptions(const std::string &game_name);

        void sortOptions(std::vector<Option> &, const std::vector<OptionDefinition> &);
    }
}

#include "button.h"
#include "analog.h"
#include "light.h"
