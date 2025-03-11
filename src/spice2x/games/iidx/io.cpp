#include "io.h"

std::vector<Button> &games::iidx::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Beatmania IIDX");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "P1 1",
                "P1 2",
                "P1 3",
                "P1 4",
                "P1 5",
                "P1 6",
                "P1 7",
                "P1 TT+",
                "P1 TT-",
                "P1 TT+/-",
                "P1 TT+/- Alternate",
                "P1 Start",
                "P2 1",
                "P2 2",
                "P2 3",
                "P2 4",
                "P2 5",
                "P2 6",
                "P2 7",
                "P2 TT+",
                "P2 TT-",
                "P2 TT+/-",
                "P2 TT+/- Alternate",
                "P2 Start",
                "EFFECT",
                "VEFX",
                "P1 Headphone",
                "P2 Headphone"
        );
    }

    return buttons;
}

std::string games::iidx::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "           P1   EFCT    P2          \n"
        "                VEFX                \n"
        "       2 4 6            2 4 6       \n"
        "  TT  1 3 5 7          1 3 5 7   TT \n"
        "\n"
        "For controllers, use Analog tab for turntable (TT)."
        ;
}

std::vector<Analog> &games::iidx::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Beatmania IIDX");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Turntable P1",
                "Turntable P2",
                "VEFX",
                "Low-EQ",
                "Hi-EQ",
                "Filter",
                "Play Volume"
        );
    }
    return analogs;
}

std::vector<Light> &games::iidx::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Beatmania IIDX");

        GameAPI::Lights::sortLights(
                &lights,
                "P1 1",
                "P1 2",
                "P1 3",
                "P1 4",
                "P1 5",
                "P1 6",
                "P1 7",
                "P2 1",
                "P2 2",
                "P2 3",
                "P2 4",
                "P2 5",
                "P2 6",
                "P2 7",
                "P1 Start",
                "P2 Start",
                "VEFX",
                "Effect",
                "Spot Light 1",
                "Spot Light 2",
                "Spot Light 3",
                "Spot Light 4",
                "Spot Light 5",
                "Spot Light 6",
                "Spot Light 7",
                "Spot Light 8",
                "Neon Lamp",
                "Woofer R",
                "Woofer G",
                "Woofer B",
                "IC Card Reader P1 R",
                "IC Card Reader P1 G",
                "IC Card Reader P1 B",
                "IC Card Reader P2 R",
                "IC Card Reader P2 G",
                "IC Card Reader P2 B",
                "Turntable P1 R",
                "Turntable P1 G",
                "Turntable P1 B",
                "Turntable P2 R",
                "Turntable P2 G",
                "Turntable P2 B",
                "Turntable P1 Resistance",
                "Turntable P2 Resistance",
                "Stage Left Avg R",
                "Stage Left Avg G",
                "Stage Left Avg B",
                "Stage Right Avg R",
                "Stage Right Avg G",
                "Stage Right Avg B",
                "Cabinet Left Avg R",
                "Cabinet Left Avg G",
                "Cabinet Left Avg B",
                "Cabinet Right Avg R",
                "Cabinet Right Avg G",
                "Cabinet Right Avg B",
                "Control Panel Under Avg R",
                "Control Panel Under Avg G",
                "Control Panel Under Avg B",
                "Ceiling Left Avg R",
                "Ceiling Left Avg G",
                "Ceiling Left Avg B",
                "Title Left Avg R",
                "Title Left Avg G",
                "Title Left Avg B",
                "Title Right Avg R",
                "Title Right Avg G",
                "Title Right Avg B",
                "Ceiling Right Avg R",
                "Ceiling Right Avg G",
                "Ceiling Right Avg B",
                "Touch Panel Left Avg R",
                "Touch Panel Left Avg G",
                "Touch Panel Left Avg B",
                "Touch Panel Right Avg R",
                "Touch Panel Right Avg G",
                "Touch Panel Right Avg B",
                "Side Panel Left Inner Avg R",
                "Side Panel Left Inner Avg G",
                "Side Panel Left Inner Avg B",
                "Side Panel Left Outer Avg R",
                "Side Panel Left Outer Avg G",
                "Side Panel Left Outer Avg B",
                "Side Panel Left Avg R",
                "Side Panel Left Avg G",
                "Side Panel Left Avg B",
                "Side Panel Right Outer Avg R",
                "Side Panel Right Outer Avg G",
                "Side Panel Right Outer Avg B",
                "Side Panel Right Inner Avg R",
                "Side Panel Right Inner Avg G",
                "Side Panel Right Inner Avg B",
                "Side Panel Right Avg R",
                "Side Panel Right Avg G",
                "Side Panel Right Avg B"
        );
    }

    return lights;
}
