#include "io.h"

std::vector<Button> &games::we::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Winning Eleven");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Start",
                "Up",
                "Down",
                "Left",
                "Right",
                "Button A",
                "Button B",
                "Button C",
                "Button D",
                "Button E",
                "Button F",
                "Pad Start",
                "Pad Select",
                "Pad Up",
                "Pad Down",
                "Pad Left",
                "Pad Right",
                "Pad Triangle",
                "Pad Cross",
                "Pad Square",
                "Pad Circle",
                "Pad L1",
                "Pad L2",
                "Pad L3",
                "Pad R1",
                "Pad R2",
                "Pad R3"
        );
    }

    return buttons;
}

std::vector<Analog> &games::we::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Winning Eleven");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Pad Stick Left X",
                "Pad Stick Left Y",
                "Pad Stick Right X",
                "Pad Stick Right Y"
        );
    }

    return analogs;
}

std::vector<Light> &games::we::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Winning Eleven");

        GameAPI::Lights::sortLights(
                &lights,
                "Left Red",
                "Left Green",
                "Left Blue",
                "Right Red",
                "Right Green",
                "Right Blue"
        );
    }

    return lights;
}
