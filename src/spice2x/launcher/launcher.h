#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <windows.h>

#include "cfg/option.h"

namespace rawinput {
    class RawInputManager;
}
namespace api {
    class Controller;
}

extern std::filesystem::path MODULE_PATH;
extern HANDLE LOG_FILE;
extern std::string LOG_FILE_PATH;
extern int LAUNCHER_ARGC;
extern char **LAUNCHER_ARGV;
extern std::unique_ptr<std::vector<Option>> LAUNCHER_OPTIONS;

extern std::unique_ptr<api::Controller> API_CONTROLLER;
extern std::unique_ptr<rawinput::RawInputManager> RI_MGR;
extern int main_implementation(int argc, char *argv[]);
