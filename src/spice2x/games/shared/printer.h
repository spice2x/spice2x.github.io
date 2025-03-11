#pragma once

#include <vector>
#include <string>

#include <windows.h>

namespace games::shared {

    // settings
    extern std::vector<std::string> PRINTER_PATH;
    extern std::vector<std::string> PRINTER_FORMAT;
    extern int PRINTER_JPG_QUALITY;
    extern bool PRINTER_CLEAR;
    extern bool PRINTER_OVERWRITE_FILE;

    void printer_attach();
}
