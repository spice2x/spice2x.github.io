#include <windows.h>

namespace nativetouch {
    void hook(HMODULE module);
    void refresh_contact_lifetime();

    // true once hook() has installed the native touch stack for the current game;
    // such games consume touch through the GetTouchInputInfo hook rather than spicetouch
    bool is_hooked();
}
 