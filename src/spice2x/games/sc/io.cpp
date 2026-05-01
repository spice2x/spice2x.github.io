#include "io.h"

std::vector<Button> &games::sc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Steel Chronicle");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "L1 Button",
                "L2 Button",
                "L Stick Button",
                "R1 Button",
                "R2 Button",
                "R Stick Button",
                "Jog Switch Left",
                "Jog Switch Right"
        );
    }

    return buttons;
}

std::vector<Analog> &games::sc::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Steel Chronicle");

        using GameAPI::Analogs::AnalogType;

        GameAPI::Analogs::sortAnalogsWithType(&analogs, {
            { "Left Stick X", AnalogType::LinearCentered },
            { "Left Stick Y", AnalogType::LinearCentered },
            { "Right Stick X", AnalogType::LinearCentered },
            { "Right Stick Y", AnalogType::LinearCentered }
        });
    }

    return analogs;
}

std::vector<Light> &games::sc::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Steel Chronicle");

        GameAPI::Lights::sortLights(
                &lights,
                "Center Red",
                "Center Green",
                "Center Blue",
                "Side Red",
                "Side Green",
                "Side Blue",
                "Controller Red",
                "Controller Blue"
        );
    }

    return lights;
}
