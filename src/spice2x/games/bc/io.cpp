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

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Stick X",
                "Stick Y"
        );
    }

    return analogs;
}