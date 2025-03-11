#include "io.h"

std::vector<Button> &games::ftt::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("FutureTomTom");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Pad 1",
                "Pad 2",
                "Pad 3",
                "Pad 4"
        );
    }

    return buttons;
}

std::string games::ftt::get_buttons_help() {
    // keep to max 100 characters wide
    return
        " Pad1 Pad2 Pad3 Pad4"
        "\n"
        "Drum pads are velocity-sensitive."
        ;
}

std::vector<Analog> &games::ftt::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("FutureTomTom");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Pad 1",
                "Pad 2",
                "Pad 3",
                "Pad 4"
        );
    }

    return analogs;
}

std::vector<Light> &games::ftt::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("FutureTomTom");

        GameAPI::Lights::sortLights(
                &lights,
                "Pad 1 Red",
                "Pad 1 Green",
                "Pad 1 Blue",
                "Pad 2 Red",
                "Pad 2 Green",
                "Pad 2 Blue",
                "Pad 3 Red",
                "Pad 3 Green",
                "Pad 3 Blue",
                "Pad 4 Red",
                "Pad 4 Green",
                "Pad 4 Blue"
        );
    }

    return lights;
}
