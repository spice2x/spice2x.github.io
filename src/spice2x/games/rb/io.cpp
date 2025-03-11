#include "io.h"

std::vector<Button> &games::rb::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Reflec Beat");
        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test"
        );
    }

    return buttons;
}

std::vector<Light> &games::rb::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Reflec Beat");

        GameAPI::Lights::sortLights(
                &lights,
                "Pole R",
                "Pole G",
                "Pole B",
                "Escutcheon R",
                "Escutcheon G",
                "Escutcheon B",
                "Woofer R",
                "Woofer G",
                "Woofer B",
                "Title R",
                "Title G",
                "Title B",
                "Title Up R",
                "Title Up G",
                "Title Up B"
        );
    }

    return lights;
}
