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
                "IC Reader B",

                "Button 1 R",
                "Button 1 G",
                "Button 1 B",

                "Button 2 R",
                "Button 2 G",
                "Button 2 B",

                "Button 3 R",
                "Button 3 G",
                "Button 3 B",

                "Button 4 R",
                "Button 4 G",
                "Button 4 B",

                "Button 5 R",
                "Button 5 G",
                "Button 5 B",

                "Button 6 R",
                "Button 6 G",
                "Button 6 B",

                "Button 7 R",
                "Button 7 G",
                "Button 7 B",

                "Button 8 R",
                "Button 8 G",
                "Button 8 B",

                "Button 9 R",
                "Button 9 G",
                "Button 9 B",

                "Button 10 R",
                "Button 10 G",
                "Button 10 B",

                "Button 11 R",
                "Button 11 G",
                "Button 11 B",

                "Button 12 R",
                "Button 12 G",
                "Button 12 B"
        );
    }

    return lights;
}
