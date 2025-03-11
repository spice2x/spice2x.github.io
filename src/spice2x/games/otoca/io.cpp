#include "io.h"

std::vector<Button> &games::otoca::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Otoca D'or");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Button Left",
                "Button Right",
                "Lever Up",
                "Lever Down",
                "Lever Left",
                "Lever Right"
        );
    }

    return buttons;
}

std::vector<Light> &games::otoca::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Otoca D'or");

        GameAPI::Lights::sortLights(
                &lights,
                "Left Button",
                "Right Button"
        );
    }

    return lights;
}
