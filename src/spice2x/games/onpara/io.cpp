#include "io.h"

std::vector<Button> &games::onpara::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Ongaku Paradise");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Start",
                "Headphone"
        );
    }

    return buttons;
}
