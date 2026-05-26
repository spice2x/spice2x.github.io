#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace overlay::notifications {

    // master switch for the notification system; when false, add() is a no-op.
    // controlled by the -nonotify launcher option.
    extern bool ENABLED;

    enum class Severity {
        Info,
        Success,
        Warning,
        Error,
    };

    // add a notification (thread-safe). returns the assigned id.
    uint64_t add(Severity severity, std::string text);

    // true if there is at least one notification that still needs to be drawn.
    // safe to call from the render thread without locking the underlying store.
    bool has_pending();

    // draw all active notifications and prune expired ones.
    // must be called from the ImGui render thread inside a NewFrame/EndFrame pair.
    void draw();
}
