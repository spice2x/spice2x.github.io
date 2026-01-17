#include "io.h"

std::vector<Button> &games::mfg::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Mahjong Fight Girl");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Test",
                "Service",
                "Coin Mech",
                "QButton",
                "QButton1",
                "QButton2",
                "QButton3",
                "Jack Detect",
                "Mic Detect"
        );
    }

    return buttons;
}



std::string games::mfg::get_buttons_help() {
    return "";
}

std::vector<Analog> &games::mfg::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Mahjong Fight Girl");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Joystick X",
                "Joystick Y",
                "Trackball DX",
                "Trackball DY"
        );
    }

    return analogs;
}

std::vector<Light> &games::mfg::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Mahjong Fight Girl");
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
                "Special Button"
        );
    }

    return lights;
}