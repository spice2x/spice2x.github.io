#include "io.h"

std::vector<Button> &games::shogikai::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Tenkaichi Shogikai");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Select"
        );
    }

    return buttons;
}

std::vector<Light> &games::shogikai::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Tenkaichi Shogikai");

        GameAPI::Lights::sortLights(
                &lights,
                "Left",
                "Right"
        );
    }

    return lights;
}
