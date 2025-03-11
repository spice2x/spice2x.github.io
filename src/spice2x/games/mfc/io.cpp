#include "io.h"

std::vector<Button> &games::mfc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Mahjong Fight Club");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Select",
                "Service",
                "Test",
                "Coin",
                "Joystick Up",
                "Joystick Down",
                "Joystick Enter"
        );
    }

    return buttons;
}
