#include "io.h"

std::vector<Button> &games::dea::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Dance Evolution");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "P1 Start",
                "P1 Left",
                "P1 Right",
                "P2 Start",
                "P2 Left",
                "P2 Right"
        );
    }

    return buttons;
}

std::vector<Light> &games::dea::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Dance Evolution");

        GameAPI::Lights::sortLights(
                &lights,
                "Title R",
                "Title G",
                "Title B",
                "Side Upper Left R",
                "Side Upper Left G",
                "Side Upper Left B",
                "Side Upper Right R",
                "Side Upper Right G",
                "Side Upper Right B",
                "P1 Start",
                "P1 L/R Button",
                "P2 Start",
                "P2 L/R Button",
                "Side Lower Left 1 R",
                "Side Lower Left 1 G",
                "Side Lower Left 1 B",
                "Side Lower Left 2 R",
                "Side Lower Left 2 G",
                "Side Lower Left 2 B",
                "Side Lower Left 3 R",
                "Side Lower Left 3 G",
                "Side Lower Left 3 B",
                "Side Lower Right 1 R",
                "Side Lower Right 1 G",
                "Side Lower Right 1 B",
                "Side Lower Right 2 R",
                "Side Lower Right 2 G",
                "Side Lower Right 2 B",
                "Side Lower Right 3 R",
                "Side Lower Right 3 G",
                "Side Lower Right 3 B"
        );
    }

    return lights;
}
