#include "io.h"

std::vector<Button> &games::qma::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Quiz Magic Academy");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Select",
                "Coin Mech",
                "Select 1",
                "Select 2",
                "Left",
                "Right",
                "OK",
                "Touch Keyboard - 1",
                "Touch Keyboard - 2",
                "Touch Keyboard - 3",
                "Touch Keyboard - 4",
                "Touch Keyboard - 5",
                "Touch Keyboard - 6",
                "Touch Keyboard - 7",
                "Touch Keyboard - 8",
                "Touch Keyboard - 9",
                "Touch Keyboard - 0",
                "Touch Keyboard - -",
                "Touch Keyboard - Q",
                "Touch Keyboard - W",
                "Touch Keyboard - E",
                "Touch Keyboard - R",
                "Touch Keyboard - T",
                "Touch Keyboard - Y",
                "Touch Keyboard - U",
                "Touch Keyboard - I",
                "Touch Keyboard - O",
                "Touch Keyboard - P",
                "Touch Keyboard - A",
                "Touch Keyboard - S",
                "Touch Keyboard - D",
                "Touch Keyboard - F",
                "Touch Keyboard - G",
                "Touch Keyboard - H",
                "Touch Keyboard - J",
                "Touch Keyboard - K",
                "Touch Keyboard - L",
                "Touch Keyboard - Z",
                "Touch Keyboard - X",
                "Touch Keyboard - C",
                "Touch Keyboard - V",
                "Touch Keyboard - B",
                "Touch Keyboard - N",
                "Touch Keyboard - M",
                "Touch Keyboard - Backspace",
                "Touch Keyboard - Enter"
        );
    }

    return buttons;
}

std::vector<Light> &games::qma::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Quiz Magic Academy");

        GameAPI::Lights::sortLights(
                &lights,
                "Lamp Red",
                "Lamp Green",
                "Lamp Blue",
                "Button Left",
                "Button Right",
                "Button OK"
        );
    }

    return lights;
}
