#include "io.h"

std::vector<Button> &games::scotto::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Scotto");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Start",
                "Up",
                "Down",
                "Cup 1",
                "Cup 2",
                "First Pad",
                "Pad A (Left Bottom)",
                "Pad B (Left Middle)",
                "Pad C (Left Top)",
                "Pad D (Right Top)",
                "Pad E (Right Middle)",
                "Pad F (Right Bottom)"
        );
    }

    return buttons;
}

std::vector<Light> &games::scotto::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Scotto");

        GameAPI::Lights::sortLights(
                &lights,
                "First Pad R",
                "First Pad G",
                "First Pad B",
                "Pad A R",
                "Pad A G",
                "Pad A B",
                "Pad B R",
                "Pad B G",
                "Pad B B",
                "Pad C R",
                "Pad C G",
                "Pad C B",
                "Pad D R",
                "Pad D G",
                "Pad D B",
                "Pad E R",
                "Pad E G",
                "Pad E B",
                "Pad F R",
                "Pad F G",
                "Pad F B",
                "Cup R",
                "Cup G",
                "Cup B",
                "Button"
        );
    }

    return lights;
}
