#include <windows.h>

namespace nativetouch {
    using TouchInputFilter = bool (*)(const TOUCHINPUT &point, bool synthetic);

    void hook(HMODULE module);
    void refresh_contact_lifetime();
    void set_input_filter(TouchInputFilter filter);

    // true once hook() has installed the native touch stack for the current game;
    // such games consume touch through the GetTouchInputInfo hook rather than spicetouch
    bool is_hooked();
}
 