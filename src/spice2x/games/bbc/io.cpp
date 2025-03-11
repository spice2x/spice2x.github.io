#include "io.h"

std::vector<Button> &games::bbc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Bishi Bashi Channel");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "P1 R",
                "P1 G",
                "P1 B",
                "P1 Disk-",
                "P1 Disk+",
                "P1 Disk -/+ Slowdown",
                "P2 R",
                "P2 G",
                "P2 B",
                "P2 Disk-",
                "P2 Disk+",
                "P2 Disk -/+ Slowdown",
                "P3 R",
                "P3 G",
                "P3 B",
                "P3 Disk-",
                "P3 Disk+",
                "P3 Disk -/+ Slowdown",
                "P4 R",
                "P4 G",
                "P4 B",
                "P4 Disk-",
                "P4 Disk+",
                "P4 Disk -/+ Slowdown"
        );
    }

    return buttons;
}

std::string games::bbc::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "     green       \n"
        " red       blue  \n"
        "\n"
        "Green button is a spinning disk that can be pressed."
        ;
}

std::vector<Analog> &games::bbc::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Bishi Bashi Channel");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "P1 Disk",
                "P2 Disk",
                "P3 Disk",
                "P4 Disk"
        );
    }

    return analogs;
}

std::vector<Light> &games::bbc::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Bishi Bashi Channel");

        GameAPI::Lights::sortLights(
                &lights,
                "P1 R",
                "P1 B",
                "P1 Disc R",
                "P1 Disc G",
                "P1 Disc B",
                "P2 R",
                "P2 B",
                "P2 Disc R",
                "P2 Disc G",
                "P2 Disc B",
                "P3 R",
                "P3 B",
                "P3 Disc R",
                "P3 Disc G",
                "P3 Disc B",
                "P4 R",
                "P4 B",
                "P4 Disc R",
                "P4 Disc G",
                "P4 Disc B",
                "IC Card R",
                "IC Card G",
                "IC Card B",
                "Under LED1 R",
                "Under LED1 G",
                "Under LED1 B",
                "Under LED2 R",
                "Under LED2 G",
                "Under LED2 B",
                "Under LED3 R",
                "Under LED3 G",
                "Under LED3 B"
        );
    }

    return lights;
}
