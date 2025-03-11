#include "qma.h"

#include <cstring>

#include <d3d9.h>

#include "games/shared/lcdhandle.h"
#include "games/shared/twtouch.h"
#include "hooks/graphics/graphics.h"
#include "hooks/devicehook.h"
#include "launcher/launcher.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/utils.h"

#include "ezusb.h"

namespace games::qma {

    /**
     * Overridden touchscreen for attaching the touch hooks to the window.
     */
    class QMATouchDevice : public games::shared::TwTouchDevice {
    public:
        bool open(LPCWSTR lpFileName) override {

            // check if device was opened
            auto result = TwTouchDevice::open(lpFileName);
            if (result) {
                if (GRAPHICS_WINDOWED) {

                    // get game window
                    HWND wnd = GetForegroundWindow();
                    if (!string_begins_with(GetActiveWindowTitle(), "QMA")) {
                        wnd = FindWindowBeginsWith("QMA");
                    }

                    if (wnd != nullptr) {

                        // get client area
                        RECT rect {};
                        GetClientRect(wnd, &rect);

                        // remove style borders
                        LONG lStyle = GetWindowLong(wnd, GWL_STYLE);
                        lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
                        SetWindowLongPtr(wnd, GWL_STYLE, lStyle);

                        // remove ex style borders
                        LONG lExStyle = GetWindowLong(wnd, GWL_EXSTYLE);
                        lExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
                        SetWindowLongPtr(wnd, GWL_EXSTYLE, lExStyle);

                        // update window
                        AdjustWindowRect(&rect, lStyle, FALSE);
                        SetWindowPos(wnd, nullptr,
                                rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
                    }
                }

                // attach touch
                touch_attach_dx_hook();

                // cursor
                if (!is_touch_available()) {
                    ShowCursor(true);
                }
            }

            // return result
            return result;
        }
    };

    /*
     * The game likes to crash if E:\LMA\quiz13 does not exist.
     * Therefore we create it when the game tries to access E:\LMA the first time.
     */
    namespace dirfix {

        static decltype(CreateFileW) *CreateFileW_real = nullptr;

        static HANDLE WINAPI CreateFileW_Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                              LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                              DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {

            // create quiz directory
            static bool dirs_created = false;
            if (!dirs_created && _wcsnicmp(lpFileName, L"E:\\LMA", 6) == 0) {
                fileutils::dir_create_recursive("E:\\LMA\\quiz11");
                fileutils::dir_create_recursive("E:\\LMA\\quiz13");
                fileutils::dir_create_recursive("E:\\LMA\\quiz15");
                dirs_created = true;
            }

            // return result
            return CreateFileW_real(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                    dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        }

        void apply() {
            CreateFileW_real = detour::iat_try("CreateFileW", CreateFileW_Hook);
        }
    }

    QMAGame::QMAGame() : Game("Quiz Magic Academy") {
    }

    void QMAGame::pre_attach() {
        Game::pre_attach();

        // load without resolving references
        // makes game not trigger DLLMain since that does some EZUSB device stuff
        LoadLibraryExW((MODULE_PATH / "ezusb.dll").c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    }

    void QMAGame::attach() {
        Game::attach();

        // init device hooks
        devicehook_init();
        devicehook_add(new games::shared::LCDHandle());
        devicehook_add(new games::qma::QMATouchDevice());

        // EZUSB for button input
        ezusb_init();

        // disable multi-head mode
        D3D9_BEHAVIOR_DISABLE = D3DCREATE_ADAPTERGROUP_DEVICE;

        // directory fix
        dirfix::apply();
    }
}
