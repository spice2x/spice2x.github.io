#include "io.h"

std::vector<Button> &games::pc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Polaris Chord");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Button 1",
                "Button 2",
                "Button 3",
                "Button 4",
                "Button 5",
                "Button 6",
                "Button 7",
                "Button 8",
                "Button 9",
                "Button 10",
                "Button 11",
                "Button 12",
                "Fader-L Left",
                "Fader-L Right",
                "Fader-R Left",
                "Fader-R Right",
                "Headphone"
        );
    }

    return buttons;
}

std::string games::pc::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "    FADER-L   FADER-R  \n"
        "\n"
        " B1 B2 B3  ...  B11 B12\n"
        "\n"
        " Most menu interactions are on the touch screen; you can use your mouse.\n"
        ;
}

std::vector<Analog> &games::pc::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Polaris Chord");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Fader-L",
                "Fader-R"
        );
    }

    return analogs;
}

std::vector<Light> &games::pc::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Polaris Chord");

        GameAPI::Lights::sortLights(
                &lights,
                "IC Reader R",
                "IC Reader G",
                "IC Reader B"
        );
    }

    return lights;
}
