#include "io.h"

std::vector<Button> &games::museca::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Museca");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Start",
                "Disk1-",
                "Disk1+",
                "Disk1 Press",
                "Disk2-",
                "Disk2+",
                "Disk2 Press",
                "Disk3-",
                "Disk3+",
                "Disk3 Press",
                "Disk4-",
                "Disk4+",
                "Disk4 Press",
                "Disk5-",
                "Disk5+",
                "Disk5 Press",
                "Foot Pedal",
                "Analog Slowdown"
        );
    }

    return buttons;
}

std::string games::museca::get_buttons_help() {
    // keep to max 100 characters wide
    return
        " Disc1     Disc3     Disc5 \n"
        "      Disc2     Disc4      \n"
        "\n"
        "           Pedal"
        ;
}

std::vector<Analog> &games::museca::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Museca");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Disk1",
                "Disk2",
                "Disk3",
                "Disk4",
                "Disk5"
        );
    }

    return analogs;
}

std::vector<Light> &games::museca::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Museca");

        GameAPI::Lights::sortLights(
                &lights,
                "Title R",
                "Title G",
                "Title B",
                "Side R",
                "Side G",
                "Side B",
                "Spinner1 R",
                "Spinner1 G",
                "Spinner1 B",
                "Spinner2 R",
                "Spinner2 G",
                "Spinner2 B",
                "Spinner3 R",
                "Spinner3 G",
                "Spinner3 B",
                "Spinner4 R",
                "Spinner4 G",
                "Spinner4 B",
                "Spinner5 R",
                "Spinner5 G",
                "Spinner5 B",
                "Under-LED1 R",
                "Under-LED1 G",
                "Under-LED1 B",
                "Under-LED2 R",
                "Under-LED2 G",
                "Under-LED2 B",
                "Under-LED3 R",
                "Under-LED3 G",
                "Under-LED3 B"
        );
    }

    return lights;
}
