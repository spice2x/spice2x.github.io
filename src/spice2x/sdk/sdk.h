#pragma once

#include <string>
#include <windows.h>

namespace sdk {

void register_sdk_hooks(std::string dll, HINSTANCE module);
void init_sdk_modules();
void fini_sdk_modules();

}