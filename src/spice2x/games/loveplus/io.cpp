#include "io.h"

std::vector<Button> &games::loveplus::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("LovePlus");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Left",
                "Right"
        );
    }

    return buttons;
}

std::vector<Light> &games::loveplus::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("LovePlus");

        GameAPI::Lights::sortLights(
                &lights,
                "Red",
                "Green",
                "Blue",
                "Left",
                "Right"
        );
    }

    return lights;
}
