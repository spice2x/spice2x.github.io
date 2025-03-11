#include "io.h"

std::vector<Button> &games::hpm::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("HELLO! Pop'n Music");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "P1 Start",
                "P1 1",
                "P1 2",
                "P1 3",
                "P1 4",
                "P2 Start",
                "P2 1",
                "P2 2",
                "P2 3",
                "P2 4"
        );
    }

    return buttons;
}

std::string games::hpm::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "               start               \n"
        " red(1) blue(2) yellow(3) green(4) \n"
        ;
}

std::vector<Light> &games::hpm::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("HELLO! Pop'n Music");

        GameAPI::Lights::sortLights(
                &lights,
                "Speaker Red",
                "Speaker Orange",
                "Speaker Blue",
                "P1 Start",
                "P1 Red & P2 Green",
                "P1 Blue",
                "P1 Yellow",
                "P1 Green",
                "P2 Start",
                "P2 Red",
                "P2 Blue",
                "P2 Yellow"
        );
    }

    return lights;
}
