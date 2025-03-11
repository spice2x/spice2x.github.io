#include "configurator_wnd.h"

#include <windows.h>

#include "build/defs.h"
#include "launcher/shutdown.h"
#include "overlay/overlay.h"
#include "util/logging.h"
#include "cfg/configurator.h"

#include "icon.h"

static const char *CLASS_NAME = "ConfiguratorWindow";
static std::string WINDOW_TITLE;
static int WINDOW_SIZE_X = 800;
static int WINDOW_SIZE_Y = 600;
static HICON WINDOW_ICON = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(MAINICON));

cfg::ConfiguratorWindow::ConfiguratorWindow() {

    // register the window class
    WNDCLASS wc {};
    wc.lpfnWndProc = ConfiguratorWindow::window_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = NULL;
    wc.hIcon = WINDOW_ICON;
    RegisterClass(&wc);

    // determine window title
    if (cfg::CONFIGURATOR_TYPE == cfg::ConfigType::Config) {
        WINDOW_TITLE = "spice2x config (" + to_string(VERSION_STRING_CFG) + ")";
        WINDOW_SIZE_X = 800;
        WINDOW_SIZE_Y = 600;
    }

    // open window
    this->hWnd = CreateWindowEx(
            0,
            CLASS_NAME,
            WINDOW_TITLE.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL,
            NULL,
            GetModuleHandle(NULL),
            (LPVOID) this);

    if (this->hWnd) {
        overlay::USE_WM_CHAR_FOR_IMGUI_CHAR_INPUT = true;
    }
}

cfg::ConfiguratorWindow::~ConfiguratorWindow() {

    // close window
    DestroyWindow(this->hWnd);

    // unregister class
    UnregisterClass(CLASS_NAME, GetModuleHandle(NULL));
}

void cfg::ConfiguratorWindow::run() {

    // show window
    SetWindowPos(this->hWnd, HWND_TOP, 0, 0, WINDOW_SIZE_X, WINDOW_SIZE_Y, 0);
    ShowWindow(this->hWnd, SW_SHOWNORMAL);
    UpdateWindow(this->hWnd);

    // draw overlay in 60 FPS
    SetTimer(this->hWnd, 1, 1000 / 60, nullptr);

    // window loop
    BOOL ret;
    MSG msg;
    while ((ret = GetMessage(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) {
            break;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

LRESULT CALLBACK cfg::ConfiguratorWindow::window_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CHAR: {

            // input characters if overlay is active
            if (overlay::OVERLAY && overlay::OVERLAY->has_focus()) {
                overlay::OVERLAY->input_char((unsigned int) wParam);
                return true;
            }
            break;
        }
        case WM_CREATE: {

            // set user data of window to class pointer
            auto create_struct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));

            break;
        }
        case WM_CLOSE:
        case WM_DESTROY: {

            // exit process
            launcher::shutdown();
            break;
        }
        case WM_TIMER: {

            // update overlay
            if (overlay::OVERLAY) {
                overlay::OVERLAY->update();
                overlay::OVERLAY->set_active(true);
                overlay::OVERLAY->new_frame();
                overlay::OVERLAY->render();
            }

            // repaint window
            InvalidateRect(hWnd, nullptr, TRUE);
            break;
        }
        case WM_ERASEBKGND: {
            return 1;
        }
        case WM_PAINT: {

            // render overlay
            if (overlay::OVERLAY) {

                // get pixel data
                int width, height;
                uint32_t *pixel_data = overlay::OVERLAY->sw_get_pixel_data(&width, &height);
                if (width > 0 && height > 0) {

                    // create bitmap
                    HBITMAP bitmap = CreateBitmap(width, height, 1, 8 * sizeof(uint32_t), pixel_data);

                    // prepare paint
                    PAINTSTRUCT paint{};
                    HDC hdc = BeginPaint(hWnd, &paint);
                    HDC hdcMem = CreateCompatibleDC(hdc);
                    SetBkMode(hdc, TRANSPARENT);

                    // draw bitmap
                    SelectObject(hdcMem, bitmap);
                    BitBlt(hdc, paint.rcPaint.left, paint.rcPaint.top,
                            paint.rcPaint.right - paint.rcPaint.left,
                            paint.rcPaint.bottom - paint.rcPaint.top,
                            hdcMem, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);

                    // delete bitmap
                    DeleteObject(bitmap);

                    // clean up
                    DeleteDC(hdcMem);
                    EndPaint(hWnd, &paint);
                } else {
                    return DefWindowProc(hWnd, uMsg, wParam, lParam);
                }
            } else {
                return DefWindowProc(hWnd, uMsg, wParam, lParam);
            }
            break;
        }
        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}
