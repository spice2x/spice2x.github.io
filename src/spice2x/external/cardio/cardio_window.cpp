/**
 * MIT-License
 * Copyright (c) 2018 by Felix
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Modified version.
 */

#include <windows.h>
#include <stdio.h>
#include <strsafe.h>
#include <dbt.h>

extern "C" {
#include <hidsdi.h>
}

#include "cardio_hid.h"
#include "cardio_window.h"
#include "util/logging.h"
#include "util/utils.h"

static BOOL CARDIO_WINDOW_UPDATE = TRUE;

static BOOL cardio_window_register_guid(HWND hWnd, HDEVNOTIFY *hDeviceNotify) {
    DEV_BROADCAST_DEVICEINTERFACE notification_filter;

    memset(&notification_filter, 0, sizeof(DEV_BROADCAST_DEVICEINTERFACE));
    notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    HidD_GetHidGuid(&notification_filter.dbcc_classguid);

    *hDeviceNotify = RegisterDeviceNotificationW(hWnd, &notification_filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (*hDeviceNotify == NULL) {
        return FALSE;
    }

    return TRUE;
}

static INT_PTR WINAPI cardio_window_winproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HDEVNOTIFY hDeviceNotify;

    LRESULT lRet = 1;

    switch (message) {
        case WM_CREATE:
            if (!cardio_window_register_guid(hWnd, &hDeviceNotify)) {
                lRet = 0;
                return lRet;
            }
            break;

        case WM_CLOSE:
            UnregisterDeviceNotification(hDeviceNotify);
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_DEVICECHANGE: {
            if (lParam && (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
                PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR) lParam;
                switch (pHdr->dbch_devicetype) {
                    case DBT_DEVTYP_DEVICEINTERFACE: {
                        PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE) pHdr;
                        std::string dbcc_name = std::string(pDevInf->dbcc_name);
                        std::wstring dbcc_name_w = s2ws(dbcc_name);
                        if (wParam == DBT_DEVICEARRIVAL && cardio_hid_add_device(dbcc_name_w.c_str())) {
                            log_info("cardio", "detected reader: {}", dbcc_name);
                        } else {
                            cardio_hid_remove_device(dbcc_name_w.c_str());
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            break;
        }

        default:
            lRet = DefWindowProc(hWnd, message, wParam, lParam);
            break;
    }

    return lRet;
}

BOOL cardio_window_init() {
    WNDCLASSEX wnd_class;
    wnd_class.cbSize = sizeof(WNDCLASSEX);
    wnd_class.style = CS_OWNDC;
    wnd_class.hInstance = GetModuleHandle(NULL);
    wnd_class.lpfnWndProc = (WNDPROC) cardio_window_winproc;
    wnd_class.cbClsExtra = 0;
    wnd_class.cbWndExtra = 0;
    wnd_class.hIcon = NULL;
    wnd_class.hbrBackground = NULL;
    wnd_class.hCursor = NULL;
    wnd_class.lpszClassName = WND_CLASS_NAME;
    wnd_class.lpszMenuName = NULL;
    wnd_class.hIconSm = NULL;

    if (!RegisterClassEx(&wnd_class))
        return FALSE;

    return TRUE;
}

BOOL cardio_window_shutdown() {
    if (!UnregisterClass(WND_CLASS_NAME, GetModuleHandle(NULL)))
        return FALSE;

    return TRUE;
}

HWND cardio_window_create(HINSTANCE hInstance) {
    HWND hWnd = CreateWindowEx(
            0,
            WND_CLASS_NAME,
            TEXT("cardio"),
            WS_DISABLED,
            0, 0,
            CW_USEDEFAULT, CW_USEDEFAULT,
            NULL,
            NULL,
            hInstance,
            NULL);
    return hWnd;
}

BOOL cardio_window_update(HWND hWnd) {
    MSG msg;
    int ret_val;

    while (CARDIO_WINDOW_UPDATE && ((ret_val = PeekMessage(&msg, hWnd, 0, 0, PM_REMOVE)) != 0)) {
        if (ret_val == -1) {
            return FALSE;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return TRUE;
}

BOOL cardio_window_close(HWND hWnd) {
    CARDIO_WINDOW_UPDATE = FALSE;
    return PostMessage(hWnd, WM_CLOSE, 0, 0);
}
