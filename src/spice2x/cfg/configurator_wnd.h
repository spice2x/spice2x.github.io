#pragma once

#include <windows.h>

namespace cfg {

    class ConfiguratorWindow {
    public:

        HWND hWnd;

        ConfiguratorWindow();
        ~ConfiguratorWindow();

        void run();

        static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    };
}
