#include "io.h"

std::vector<Button> &games::cardconnect::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Card Connect");
        GameAPI::Buttons::sortButtons(
                &buttons,
                "Test",
                "Service",
                "Coin Mech"
        );
    }

    return buttons;
}

std::vector<Light> &games::cardconnect::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Card Connect");
        GameAPI::Lights::sortLights(
                &lights,
                "Photo Light",
                "Card Reader R",
                "Card Reader G",
                "Card Reader B",
                "Side Left R",
                "Side Left G",
                "Side Left B",
                "Side Right R",
                "Side Right G",
                "Side Right B"
        );
    }

    return lights;
}
