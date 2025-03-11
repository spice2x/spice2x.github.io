#include "io.h"

namespace games::pcm {
    std::vector <Button> &get_buttons() {
        static std::vector <Button> buttons;

        if (buttons.empty()) {
            buttons = GameAPI::Buttons::getButtons("Charge Machine");
            GameAPI::Buttons::sortButtons(
                    &buttons,
                    "Service",
                    "Test",
                    "Insert 1000 Yen Bill",
                    "Insert 2000 Yen Bill",
                    "Insert 5000 Yen Bill",
                    "Insert 10000 Yen Bill"
            );
        }

        return buttons;
    }
}
