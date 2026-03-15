#pragma once

#include <cstdlib>

namespace overlay::windows {

    struct LightMatchMap {
        const char *controller;     // controller name for display
        const char *game;           // game name to match
        unsigned short vid;         // USB vendor ID filter, or 0 to skip
        unsigned short pid;         // USB product ID filter, or 0 to skip
        const char *device_light;   // light name (or prefix if rgb)
        const char *device_suffix;  // after R/G/B for rgb mode, or ""
        const char *address;        // control index for address mode, or ""
        bool rgb;                   // expand " R/G/B" between device_light and device_suffix
        const char *game_light;     // target game light (or prefix if rgb), without P1/P2 prefix
        const char *game_light_alt; // alt game light for Valkyrie mode, or ""
    };

    static const LightMatchMap LIGHT_MATCH_MAP[] = {

        // FAUCETWO - Sound Voltex (RGB)
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 6/24", "", true, "Wing Left Up", "Upper Left Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 12/24", "", true, "Wing Left Low", "Lower Left Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 18/24", "", true, "Wing Right Low", "Lower Right Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 24/24", "", true, "Wing Right Up", "Upper Right Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Left and Right", "", "", true, "Controller", "Control Panel Avg"},

        // FAUCETWO Portable - Sound Voltex (RGB)
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Left Side", "", "", true, "Wing Left Up", "Left Wing Avg"},
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Right Side", "", "", true, "Wing Right Up", "Right Wing Avg"},
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Center1", "", "", true, "Controller", "Control Panel Avg"},
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Center2", "", "", true, "Woofer", "Woofer Avg"},

        // PHOENIXWAN - Beatmania IIDX (address based, VID:PID 034C:0368)
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x00", false, "1", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x01", false, "2", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x02", false, "3", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x03", false, "4", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x04", false, "5", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x05", false, "6", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x06", false, "7", ""},
    };

    static const int LIGHT_MATCH_MAP_COUNT =
        sizeof(LIGHT_MATCH_MAP) / sizeof(LIGHT_MATCH_MAP[0]);

}
