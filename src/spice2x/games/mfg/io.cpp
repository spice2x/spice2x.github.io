#include "io.h"

std::vector<Button> &games::mfg::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Mahjong Fight Girl");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech"
                //"Action Button"
        );
    }

    return buttons;
}

std::string games::mfg::get_buttons_help() {
    return "";
}
