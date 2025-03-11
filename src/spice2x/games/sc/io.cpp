#include "io.h"

std::vector<Button> &games::sc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Steel Chronicle");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "L1 Button",
                "L2 Button",
                "L Stick Button",
                "R1 Button",
                "R2 Button",
                "R Stick Button",
                "Jog Switch Left",
                "Jog Switch Right"
        );
    }

    return buttons;
}

std::vector<Analog> &games::sc::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Steel Chronicle");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Left Stick X",
                "Left Stick Y",
                "Right Stick X",
                "Right Stick Y"
        );
    }

    return analogs;
}

std::vector<Light> &games::sc::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Steel Chronicle");

        GameAPI::Lights::sortLights(
                &lights,
                "Center Red",
                "Center Green",
                "Center Blue",
                "Side Red",
                "Side Green",
                "Side Blue",
                "Controller Red",
                "Controller Blue"
        );
    }

    return lights;
}
