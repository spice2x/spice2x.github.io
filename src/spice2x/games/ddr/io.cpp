#include "io.h"

std::vector<Button> &games::ddr::get_buttons() {
    static std::vector<Button> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Buttons::getButtons("Dance Dance Revolution");

        GameAPI::Buttons::sortButtons(
                &analogs,
                "Service",
                "Test",
                "Coin Mech",
                "P1 Start",
                "P1 Panel Up",
                "P1 Panel Down",
                "P1 Panel Left",
                "P1 Panel Right",
                "P1 Menu Up",
                "P1 Menu Down",
                "P1 Menu Left",
                "P1 Menu Right",
                "P2 Start",
                "P2 Panel Up",
                "P2 Panel Down",
                "P2 Panel Left",
                "P2 Panel Right",
                "P2 Menu Up",
                "P2 Menu Down",
                "P2 Menu Left",
                "P2 Menu Right"
        );
    }

    return analogs;
}

std::vector<Light> &games::ddr::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Dance Dance Revolution");

        GameAPI::Lights::sortLights(
                &lights,
                "P1 Foot Left",
                "P1 Foot Up",
                "P1 Foot Right",
                "P1 Foot Down",
                "P2 Foot Left",
                "P2 Foot Up",
                "P2 Foot Right",
                "P2 Foot Down",
                "Spot Red",
                "Spot Blue",
                "Top Spot Red",
                "Top Spot Blue",
                "P1 Halogen Upper",
                "P1 Halogen Lower",
                "P2 Halogen Upper",
                "P2 Halogen Lower",
                "P1 Button",
                "P2 Button",
                "Neon",
                "HD P1 Start",
                "HD P1 Menu Left-Right",
                "HD P1 Menu Up-Down",
                "HD P2 Start",
                "HD P2 Menu Left-Right",
                "HD P2 Menu Up-Down",
                "HD P1 Speaker F R",
                "HD P1 Speaker F G",
                "HD P1 Speaker F B",
                "HD P1 Speaker W R",
                "HD P1 Speaker W G",
                "HD P1 Speaker W B",
                "HD P2 Speaker F R",
                "HD P2 Speaker F G",
                "HD P2 Speaker F B",
                "HD P2 Speaker W R",
                "HD P2 Speaker W G",
                "HD P2 Speaker W B",

                "WHITE Speaker Top R",
                "WHITE Speaker Top G",
                "WHITE Speaker Top B",
                "WHITE Speaker Bottom R",
                "WHITE Speaker Bottom G",
                "WHITE Speaker Bottom B",
                "WHITE Woofer R",
                "WHITE Woofer G",
                "WHITE Woofer B",

                "GOLD P1 Menu Start",
                "GOLD P1 Menu Up",
                "GOLD P1 Menu Down",
                "GOLD P1 Menu Left",
                "GOLD P1 Menu Right",

                "GOLD P2 Menu Start",
                "GOLD P2 Menu Up",
                "GOLD P2 Menu Down",
                "GOLD P2 Menu Left",
                "GOLD P2 Menu Right",

                "GOLD Title Panel Left",
                "GOLD Title Panel Center",
                "GOLD Title Panel Right",

                "GOLD P1 Woofer Corner",
                "GOLD P2 Woofer Corner",

                "GOLD P1 Card Unit R",
                "GOLD P1 Card Unit G",
                "GOLD P1 Card Unit B",

                "GOLD P2 Card Unit R",
                "GOLD P2 Card Unit G",
                "GOLD P2 Card Unit B",

                "GOLD Top Panel Avg R",
                "GOLD Top Panel Avg G",
                "GOLD Top Panel Avg B",

                "GOLD Monitor Side Left Avg R",
                "GOLD Monitor Side Left Avg G",
                "GOLD Monitor Side Left Avg B",

                "GOLD Monitor Side Right Avg R",
                "GOLD Monitor Side Right Avg G",
                "GOLD Monitor Side Right Avg B",

                "GOLD P1 Foot Up Avg R",
                "GOLD P1 Foot Up Avg G",
                "GOLD P1 Foot Up Avg B",

                "GOLD P1 Foot Down Avg R",
                "GOLD P1 Foot Down Avg G",
                "GOLD P1 Foot Down Avg B",

                "GOLD P1 Foot Left Avg R",
                "GOLD P1 Foot Left Avg G",
                "GOLD P1 Foot Left Avg B",

                "GOLD P1 Foot Right Avg R",
                "GOLD P1 Foot Right Avg G",
                "GOLD P1 Foot Right Avg B",

                "GOLD P2 Foot Up Avg R",
                "GOLD P2 Foot Up Avg G",
                "GOLD P2 Foot Up Avg B",

                "GOLD P2 Foot Down Avg R",
                "GOLD P2 Foot Down Avg G",
                "GOLD P2 Foot Down Avg B",

                "GOLD P2 Foot Left Avg R",
                "GOLD P2 Foot Left Avg G",
                "GOLD P2 Foot Left Avg B",

                "GOLD P2 Foot Right Avg R",
                "GOLD P2 Foot Right Avg G",
                "GOLD P2 Foot Right Avg B",

                "GOLD P1 Stage Corner Up-Left",
                "GOLD P1 Stage Corner Up-Right",
                "GOLD P1 Stage Corner Down-Left",
                "GOLD P1 Stage Corner Down-Right",

                "GOLD P2 Stage Corner Up-Left",
                "GOLD P2 Stage Corner Up-Right",
                "GOLD P2 Stage Corner Down-Left",
                "GOLD P2 Stage Corner Down-Right"
        );
    }

    return lights;
}
