#include "io.h"

std::vector<Button> &games::bc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Busou Shinki: Armored Princess Battle Conductor");

        GameAPI::Buttons::sortButtons(
            &buttons,
            "Service",
            "Test",
            "Up",
            "Down",
            "Left",
            "Right",
            "Joystick Button",
            "Trigger 1",
            "Trigger 2",
            "Button 1",
            "Button 2",
            "Button 3",
            "Button 4"
        );
    }

    return buttons;
}

std::string games::bc::get_buttons_help() {
    // keep to max 100 characters wide
    return "";
}

std::vector<Analog> &games::bc::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Busou Shinki: Armored Princess Battle Conductor");

        using GameAPI::Analogs::AnalogType;

        GameAPI::Analogs::sortAnalogsWithType(&analogs, {
            { "Stick X", AnalogType::LinearCentered },
            { "Stick Y", AnalogType::LinearCentered }
        });
    }

    return analogs;
}

std::vector<Light> &games::bc::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Busou Shinki: Armored Princess Battle Conductor");

        GameAPI::Lights::sortLights(
            &lights,
            "Title Panel R",
            "Title Panel G",
            "Title Panel B",
            "Side Panel R",
            "Side Panel G",
            "Side Panel B",
            "Card Reader R",
            "Card Reader G",
            "Card Reader B",
            "Joystick R",
            "Joystick G",
            "Joystick B",
            "Button",
            "IR LED"
        );
    }

    return lights;
}
