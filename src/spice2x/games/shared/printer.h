#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <initializer_list>

#include <windows.h>

namespace games::shared {

    // settings
    extern std::vector<std::string> PRINTER_PATH;
    extern std::vector<std::string> PRINTER_FORMAT;
    extern int PRINTER_JPG_QUALITY;
    extern bool PRINTER_CLEAR;
    extern bool PRINTER_OVERWRITE_FILE;

    void printer_attach();
    struct PrinterImage {
        const uint8_t *data;
        int width;
        int height;
        int components;
        std::string suffix;
    };

    void printer_init_output(std::initializer_list<std::string> suffixes = {""});
    bool printer_write_images(std::initializer_list<PrinterImage> images);
    bool printer_write_image(const uint8_t *data, int width, int height, int components,
                             const std::string &suffix = "");
}
