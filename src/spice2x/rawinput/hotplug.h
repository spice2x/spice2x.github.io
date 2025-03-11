#pragma once

#include <windows.h>

namespace rawinput {

    class RawInputManager;

    class HotplugManager {
    private:
        RawInputManager *ri_mgr;
        HANDLE hotplug_hid;

    public:
        HotplugManager(RawInputManager *ri_mgr, HWND hwnd);
        ~HotplugManager();

        LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    };
}
