#include "io.h"

std::vector<Button> &games::bs::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Beatstream");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech"
        );
    }

    return buttons;
}

std::vector<Light> &games::bs::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Beatstream");

        GameAPI::Lights::sortLights(
                &lights,
                "Bottom R",
                "Bottom G",
                "Bottom B",
                "Left R",
                "Left G",
                "Left B",
                "Right R",
                "Right G",
                "Right B"
        );
    }

    return lights;
}
